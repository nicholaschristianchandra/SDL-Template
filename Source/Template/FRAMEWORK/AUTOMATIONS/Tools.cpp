/* Tools.exe, the build's helper commands: Includes, Compile, Shaders, Bake, Symbols, Icon, Watch, Version. Build.bat builds it. */
#include <windows.h>
#include <tlhelp32.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_PATH_LENGTH 1024
#define MAX_COMMAND_LENGTH 32768
#define READ_ATTEMPTS 8
#define READ_RETRY_DELAY_MS 150

#pragma region Containers
/* Growable lists of strings and byte buffers */

typedef struct
{
    char *Data;
    size_t Length;
    size_t Capacity;
} Buffer;

typedef struct
{
    const char *Data;
    size_t Length;
} Slice;

typedef struct
{
    char **Items;
    int Count;
    int Capacity;
} List;

static void OutOfMemory(void)
{
    fprintf(stderr, "[Tools] ERROR: Out of memory.\n");
    exit(1);
}

static void Buffer_Append(Buffer *buffer, const char *text, size_t length)
{
    if (buffer->Length + length + 1 > buffer->Capacity)
    {
        size_t capacity = buffer->Capacity ? buffer->Capacity : 256;

        while (capacity < buffer->Length + length + 1)
        {
            capacity *= 2;
        }

        buffer->Data = (char *)realloc(buffer->Data, capacity);
        buffer->Capacity = capacity;

        if (!buffer->Data)
        {
            OutOfMemory();
        }
    }

    if (length > 0)
    {
        memcpy(buffer->Data + buffer->Length, text, length);
    }

    buffer->Length += length;
    buffer->Data[buffer->Length] = '\0';
}

static void Buffer_AppendChar(Buffer *buffer, char character)
{
    Buffer_Append(buffer, &character, 1);
}

static void Buffer_AppendText(Buffer *buffer, const char *text)
{
    Buffer_Append(buffer, text, strlen(text));
}

static void Buffer_Clear(Buffer *buffer)
{
    buffer->Length = 0;

    if (buffer->Data)
    {
        buffer->Data[0] = '\0';
    }
}

static void Buffer_Free(Buffer *buffer)
{
    free(buffer->Data);
    *buffer = Buffer{};
}

static Slice Buffer_Slice(const Buffer *buffer)
{
    return Slice{buffer->Data ? buffer->Data : "", buffer->Length};
}

static void List_Add(List *list, const char *text)
{
    if (list->Count == list->Capacity)
    {
        list->Capacity = list->Capacity ? list->Capacity * 2 : 16;
        list->Items = (char **)realloc(list->Items, list->Capacity * sizeof(char *));

        if (!list->Items)
        {
            OutOfMemory();
        }
    }

    const size_t length = strlen(text);
    char *copy = (char *)malloc(length + 1);

    if (!copy)
    {
        OutOfMemory();
    }

    memcpy(copy, text, length + 1);
    list->Items[list->Count++] = copy;
}

static bool List_Contains(const List *list, const char *text)
{
    for (int i = 0; i < list->Count; i++)
    {
        if (strcmp(list->Items[i], text) == 0)
        {
            return true;
        }
    }

    return false;
}

static bool List_ContainsInsensitive(const List *list, const char *text)
{
    for (int i = 0; i < list->Count; i++)
    {
        if (_stricmp(list->Items[i], text) == 0)
        {
            return true;
        }
    }

    return false;
}

static void List_Free(List *list)
{
    for (int i = 0; i < list->Count; i++)
    {
        free(list->Items[i]);
    }

    free(list->Items);
    *list = List{};
}

#pragma endregion

#pragma region Text Helpers
/* Slices of text: trimming, splitting, comparing */

static bool IsWhitespace(char character)
{
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' || character == '\v' || character == '\f';
}

static bool IsIdentChar(char character)
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '_';
}

static bool IsIdentStartChar(char character)
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || character == '_';
}

static int CompareInsensitiveItems(const void *a, const void *b)
{
    return _stricmp(*(char *const *)a, *(char *const *)b);
}

static Slice Slice_TrimStart(Slice slice)
{
    while (slice.Length > 0 && IsWhitespace(slice.Data[0]))
    {
        slice.Data++;
        slice.Length--;
    }

    return slice;
}

static Slice Slice_TrimEnd(Slice slice)
{
    while (slice.Length > 0 && IsWhitespace(slice.Data[slice.Length - 1]))
    {
        slice.Length--;
    }

    return slice;
}

static Slice Slice_Trim(Slice slice)
{
    return Slice_TrimEnd(Slice_TrimStart(slice));
}

static bool Slice_StartsWithKeyword(Slice slice, const char *keyword)
{
    const size_t keywordLength = strlen(keyword);

    if (slice.Length < keywordLength || memcmp(slice.Data, keyword, keywordLength) != 0)
    {
        return false;
    }

    return slice.Length == keywordLength || !IsIdentChar(slice.Data[keywordLength]);
}

static bool Slice_IsBareIdentifier(Slice slice)
{
    if (slice.Length == 0 || !IsIdentStartChar(slice.Data[0]))
    {
        return false;
    }

    for (size_t i = 1; i < slice.Length; i++)
    {
        if (!IsIdentChar(slice.Data[i]))
        {
            return false;
        }
    }

    return true;
}

static bool ContainsWholeWord(const char *haystack, const char *word)
{
    const size_t haystackLength = strlen(haystack);
    const size_t wordLength = strlen(word);

    if (wordLength == 0)
    {
        return false;
    }

    for (size_t i = 0; i + wordLength <= haystackLength; i++)
    {
        if (memcmp(haystack + i, word, wordLength) != 0)
        {
            continue;
        }

        const bool beforeOk = i == 0 || !IsIdentChar(haystack[i - 1]);
        const bool afterOk = i + wordLength >= haystackLength || !IsIdentChar(haystack[i + wordLength]);

        if (beforeOk && afterOk)
        {
            return true;
        }
    }

    return false;
}

static bool EndsWithExtension(const char *path, const char *extension)
{
    const size_t pathLength = strlen(path);
    const size_t extensionLength = strlen(extension);
    return pathLength >= extensionLength && _stricmp(path + pathLength - extensionLength, extension) == 0;
}

static bool IsCodeFile(const char *path)
{
    return EndsWithExtension(path, ".h") || EndsWithExtension(path, ".hpp") || EndsWithExtension(path, ".cpp");
}

static const char *FileName(const char *path)
{
    const char *name = strrchr(path, '/');
    return name ? name + 1 : path;
}

static void ReplaceChar(char *text, char from, char to)
{
    for (char *cursor = text; *cursor; cursor++)
    {
        if (*cursor == from)
        {
            *cursor = to;
        }
    }
}

static void NormalizePath(char *path)
{
    ReplaceChar(path, '\\', '/');
    size_t length = strlen(path);

    while (length > 1 && path[length - 1] == '/')
    {
        path[--length] = '\0';
    }
}

static void AppendArgument(Buffer *command, const char *argument)
{
    if (command->Length > 0)
    {
        Buffer_AppendChar(command, ' ');
    }

    if (*argument && !strpbrk(argument, " \t\""))
    {
        Buffer_AppendText(command, argument);
        return;
    }

    Buffer_AppendChar(command, '"');

    for (const char *cursor = argument;; cursor++)
    {
        int backslashes = 0;

        while (*cursor == '\\')
        {
            backslashes++;
            cursor++;
        }

        if (*cursor == '\0')
        {
            for (int i = 0; i < backslashes * 2; i++)
            {
                Buffer_AppendChar(command, '\\');
            }

            break;
        }

        const int count = *cursor == '"' ? backslashes * 2 + 1 : backslashes;

        for (int i = 0; i < count; i++)
        {
            Buffer_AppendChar(command, '\\');
        }

        Buffer_AppendChar(command, *cursor);
    }

    Buffer_AppendChar(command, '"');
}

static void AppendPathArgument(Buffer *command, const char *prefix, const char *path)
{
    char argument[MAX_PATH_LENGTH + 16];
    snprintf(argument, sizeof(argument), "%s%s", prefix, path);
    ReplaceChar(argument + strlen(prefix), '/', '\\');
    AppendArgument(command, argument);
}

#pragma endregion

#pragma region File System
/* Paths, reading files, writing them only when their content changes, walking folders */

static bool DirectoryExists(const char *path)
{
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool FileExists(const char *path)
{
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

static bool GetWriteTime(const char *path, ULONGLONG *time)
{
    WIN32_FILE_ATTRIBUTE_DATA data;

    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data) || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        return false;
    }

    *time = ((ULONGLONG)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
    return true;
}

static void EnsureDirectory(const char *path)
{
    char partial[MAX_PATH_LENGTH];
    snprintf(partial, sizeof(partial), "%s", path);

    for (char *cursor = partial + 1; *cursor; cursor++)
    {
        if (*cursor == '/')
        {
            *cursor = '\0';
            CreateDirectoryA(partial, NULL);
            *cursor = '/';
        }
    }

    CreateDirectoryA(partial, NULL);
}

static void EnsureParentDirectory(const char *path)
{
    char directory[MAX_PATH_LENGTH];
    snprintf(directory, sizeof(directory), "%s", path);
    char *lastSlash = strrchr(directory, '/');

    if (lastSlash)
    {
        *lastSlash = '\0';
        EnsureDirectory(directory);
    }
}

static void ListEntries(const char *directory, List *files, List *directories)
{
    char pattern[MAX_PATH_LENGTH];
    snprintf(pattern, sizeof(pattern), "%s/*", directory);

    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA(pattern, &data);

    if (handle == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0)
        {
            continue;
        }

        char path[MAX_PATH_LENGTH];
        snprintf(path, sizeof(path), "%s/%s", directory, data.cFileName);

        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (directories)
            {
                List_Add(directories, path);
            }

            ListEntries(path, files, directories);
        }
        else if (files)
        {
            List_Add(files, path);
        }
    }
    while (FindNextFileA(handle, &data));

    FindClose(handle);
}

/* The files in directory (not its subfolders) whose names match pattern, like "*.vcxproj" */
static void ListMatches(const char *directory, const char *pattern, List *files)
{
    char search[MAX_PATH_LENGTH];
    snprintf(search, sizeof(search), "%s/%s", directory, pattern);
    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA(search, &data);

    for (bool more = handle != INVALID_HANDLE_VALUE; more; more = FindNextFileA(handle, &data))
    {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            char path[MAX_PATH_LENGTH];
            snprintf(path, sizeof(path), "%s/%s", directory, data.cFileName);
            List_Add(files, path);
        }
    }

    if (handle != INVALID_HANDLE_VALUE)
    {
        FindClose(handle);
    }
}

/* FNV-1a of a path, ignoring case */
static unsigned int HashPath(const char *path)
{
    unsigned int hash = 2166136261u;

    for (const char *cursor = path; *cursor; cursor++)
    {
        hash = (hash ^ (unsigned char)tolower((unsigned char)*cursor)) * 16777619u;
    }

    return hash;
}

static bool ReadWholeFile(const char *path, Buffer *contents)
{
    FILE *file = fopen(path, "rb");

    for (int attempt = 1; !file && attempt < READ_ATTEMPTS && FileExists(path); attempt++)
    {
        Sleep(READ_RETRY_DELAY_MS);
        file = fopen(path, "rb");
    }

    if (!file)
    {
        return false;
    }

    char chunk[4096];
    size_t read = 0;

    while ((read = fread(chunk, 1, sizeof(chunk), file)) > 0)
    {
        Buffer_Append(contents, chunk, read);
    }

    fclose(file);
    return true;
}

static int UpdatedCount = 0;

static bool WriteIfChanged(const char *path, const Buffer *text)
{
    Buffer current = {};

    if (FileExists(path) && ReadWholeFile(path, &current))
    {
        const bool same = current.Length == text->Length && (text->Length == 0 || memcmp(current.Data, text->Data, text->Length) == 0);
        Buffer_Free(&current);

        if (same)
        {
            return true;
        }
    }

    EnsureParentDirectory(path);

    char temporaryPath[MAX_PATH_LENGTH];
    snprintf(temporaryPath, sizeof(temporaryPath), "%s.%lu.tmp", path, (unsigned long)GetCurrentProcessId());

    FILE *file = fopen(temporaryPath, "wb");

    if (!file)
    {
        return false;
    }

    const size_t written = text->Length ? fwrite(text->Data, 1, text->Length, file) : 0;
    fclose(file);

    if (written != text->Length || !MoveFileExA(temporaryPath, path, MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileA(temporaryPath);
        return false;
    }

    UpdatedCount++;
    return true;
}

/* Deletes the files in directory that are not in kept, then the folders left empty */
static void DeleteStale(const char *directory, const List *kept)
{
    List existing = {};
    List directories = {};
    ListEntries(directory, &existing, &directories);

    for (int i = 0; i < existing.Count; i++)
    {
        if (!List_ContainsInsensitive(kept, existing.Items[i]) && DeleteFileA(existing.Items[i]))
        {
            UpdatedCount++;
        }
    }

    for (int i = directories.Count - 1; i >= 0; i--)
    {
        RemoveDirectoryA(directories.Items[i]);
    }

    List_Free(&directories);
    List_Free(&existing);
}

#pragma endregion

#pragma region Jobs
/* Runs commands in parallel, one per CPU core, and collects their output */

typedef struct
{
    char Name[MAX_PATH_LENGTH];
    char Log[MAX_PATH_LENGTH];
    Buffer Command;
    HANDLE Process;
    bool Failed;
} Job;

static bool Job_Start(Job *job)
{
    if (job->Command.Length >= MAX_COMMAND_LENGTH)
    {
        fprintf(stderr, "[Tools] ERROR: The command for %s is too long.\n", job->Name);
        return false;
    }

    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    EnsureParentDirectory(job->Log);
    HANDLE log = CreateFileA(job->Log, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (log == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "[Tools] ERROR: Could not create \"%s\"\n", job->Log);
        return false;
    }

    STARTUPINFOA startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = log;
    startup.hStdError = log;

    PROCESS_INFORMATION process = {};
    const bool started = CreateProcessA(NULL, job->Command.Data, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process);
    CloseHandle(log);

    if (!started)
    {
        fprintf(stderr, "[Tools] ERROR: Could not start the compiler for %s (error %lu). Is it on PATH?\n", job->Name, (unsigned long)GetLastError());
        DeleteFileA(job->Log);
        return false;
    }

    CloseHandle(process.hThread);
    job->Process = process.hProcess;
    return true;
}

static void Job_Finish(Job *job)
{
    DWORD exitCode = 1;
    GetExitCodeProcess(job->Process, &exitCode);
    CloseHandle(job->Process);
    job->Process = NULL;
    job->Failed = exitCode != 0;

    Buffer output = {};

    if (ReadWholeFile(job->Log, &output) && output.Length > 0)
    {
        fwrite(output.Data, 1, output.Length, stdout);

        if (output.Data[output.Length - 1] != '\n')
        {
            fputc('\n', stdout);
        }
    }

    Buffer_Free(&output);
    DeleteFileA(job->Log);
}

static bool RunJobs(Job **jobs, int count, void (*finished)(Job *job))
{
    SYSTEM_INFO system = {};
    GetSystemInfo(&system);
    int parallel = system.dwNumberOfProcessors > 0 ? (int)system.dwNumberOfProcessors : 1;

    if (parallel > MAXIMUM_WAIT_OBJECTS)
    {
        parallel = MAXIMUM_WAIT_OBJECTS;
    }

    HANDLE handles[MAXIMUM_WAIT_OBJECTS];
    Job *running[MAXIMUM_WAIT_OBJECTS];
    int runningCount = 0;
    int next = 0;
    bool failed = false;

    while (true)
    {
        while (!failed && runningCount < parallel && next < count)
        {
            Job *job = jobs[next++];

            if (!Job_Start(job))
            {
                job->Failed = true;
                failed = true;
                break;
            }

            handles[runningCount] = job->Process;
            running[runningCount++] = job;
        }

        if (runningCount == 0)
        {
            return !failed;
        }

        const DWORD signaled = WaitForMultipleObjects((DWORD)runningCount, handles, FALSE, INFINITE);
        const int slot = signaled - WAIT_OBJECT_0 < (DWORD)runningCount ? (int)(signaled - WAIT_OBJECT_0) : 0;
        Job *job = running[slot];

        Job_Finish(job);
        finished(job);
        failed |= job->Failed;

        handles[slot] = handles[runningCount - 1];
        running[slot] = running[runningCount - 1];
        runningCount--;
    }
}

#pragma endregion

#pragma region Includes: Scanning
/* Reads the scripts' declarations (types, functions, globals) without a compiler */

static void SpliceContinuations(Slice text, Buffer *output)
{
    size_t i = 0;

    while (i < text.Length)
    {
        if (text.Data[i] == '\\' && i + 1 < text.Length && (text.Data[i + 1] == '\n' || text.Data[i + 1] == '\r'))
        {
            if (text.Data[i + 1] == '\r' && i + 2 < text.Length && text.Data[i + 2] == '\n')
            {
                i += 3;
            }
            else
            {
                i += 2;
            }

            continue;
        }

        Buffer_AppendChar(output, text.Data[i]);
        i++;
    }
}

static size_t RawStringEnd(Slice text, size_t index)
{
    if (index == 0 || text.Data[index - 1] != 'R')
    {
        return 0;
    }

    size_t start = index - 1;

    if (start >= 2 && text.Data[start - 2] == 'u' && text.Data[start - 1] == '8')
    {
        start -= 2;
    }
    else if (start >= 1 && (text.Data[start - 1] == 'u' || text.Data[start - 1] == 'U' || text.Data[start - 1] == 'L'))
    {
        start -= 1;
    }

    if (start > 0 && IsIdentChar(text.Data[start - 1]))
    {
        return 0;
    }

    char delimiter[20];
    size_t length = 0;
    size_t i = index + 1;

    while (i < text.Length && text.Data[i] != '(' && length < 16)
    {
        delimiter[length++] = text.Data[i++];
    }

    if (i >= text.Length || text.Data[i] != '(')
    {
        return 0;
    }

    for (i++; i < text.Length; i++)
    {
        if (text.Data[i] == ')' && i + 1 + length < text.Length && strncmp(text.Data + i + 1, delimiter, length) == 0 && text.Data[i + 1 + length] == '"')
        {
            return i + length + 2;
        }
    }

    return text.Length;
}

static bool IsDigitSeparator(Slice text, size_t index)
{
    if (index == 0 || index + 1 >= text.Length || !IsIdentChar(text.Data[index - 1]) || !IsIdentChar(text.Data[index + 1]))
    {
        return false;
    }

    size_t start = index;

    while (start > 0 && (IsIdentChar(text.Data[start - 1]) || text.Data[start - 1] == '\''))
    {
        start--;
    }

    return text.Data[start] >= '0' && text.Data[start] <= '9';
}

static int ConstantCondition(const char *expression)
{
    while (*expression == ' ' || *expression == '\t')
    {
        expression++;
    }

    const char *end = expression + strlen(expression);

    while (end > expression && (end[-1] == ' ' || end[-1] == '\t'))
    {
        end--;
    }

    const size_t length = (size_t)(end - expression);

    if ((length == 1 && *expression == '0') || (length == 5 && strncmp(expression, "false", 5) == 0))
    {
        return 0;
    }

    if ((length == 1 && *expression == '1') || (length == 4 && strncmp(expression, "true", 4) == 0))
    {
        return 1;
    }

    return -1;
}

static bool DirectiveIs(const char *line, const char *name)
{
    const size_t length = strlen(name);
    return strncmp(line, name, length) == 0 && (line[length] == '\0' || line[length] == ' ' || line[length] == '\t' || line[length] == '(');
}

static void DropDisabledCode(Buffer *text, size_t start)
{
    enum { MaxDepth = 64 };
    bool constant[MaxDepth];
    bool taking[MaxDepth];
    bool taken[MaxDepth];
    bool undecided[MaxDepth];
    int depth = 0;
    size_t write = start;
    size_t read = start;

    while (read < text->Length)
    {
        size_t end = read;

        while (end < text->Length && text->Data[end] != '\n')
        {
            end++;
        }

        const char *cursor = text->Data + read;

        while (cursor < text->Data + end && (*cursor == ' ' || *cursor == '\t'))
        {
            cursor++;
        }

        bool active = true;

        for (int level = 0; level < depth; level++)
        {
            active = active && taking[level];
        }

        bool keep = active;

        if (cursor < text->Data + end && *cursor == '#')
        {
            char line[512];
            cursor++;

            while (cursor < text->Data + end && (*cursor == ' ' || *cursor == '\t'))
            {
                cursor++;
            }

            snprintf(line, sizeof(line), "%.*s", (int)(text->Data + end - cursor), cursor);
            line[strcspn(line, "\r")] = '\0';

            if ((DirectiveIs(line, "if") || DirectiveIs(line, "ifdef") || DirectiveIs(line, "ifndef")) && depth < MaxDepth)
            {
                const int value = DirectiveIs(line, "if") ? ConstantCondition(line + 2) : -1;
                constant[depth] = value >= 0;
                taking[depth] = value != 0;
                taken[depth] = value == 1;
                undecided[depth] = false;
                keep = active && !constant[depth];
                depth++;
            }
            else if ((DirectiveIs(line, "elif") || DirectiveIs(line, "else")) && depth > 0 && constant[depth - 1])
            {
                const int level = depth - 1;
                const int value = DirectiveIs(line, "else") ? 1 : ConstantCondition(line + 4);
                undecided[level] = undecided[level] || (value < 0 && !taken[level]);
                taking[level] = undecided[level] || (!taken[level] && value == 1);
                taken[level] = taken[level] || value == 1;
                keep = false;
            }
            else if (DirectiveIs(line, "endif") && depth > 0)
            {
                depth--;
                keep = active && !constant[depth];
            }
        }

        const size_t lineLength = end - read;

        if (keep)
        {
            memmove(text->Data + write, text->Data + read, lineLength);
            write += lineLength;
        }

        if (end < text->Length)
        {
            text->Data[write++] = '\n';
        }

        read = end + 1;
    }

    text->Length = write;

    if (text->Data)
    {
        text->Data[write] = '\0';
    }
}

static void StripComments(Slice text, Buffer *output)
{
    const size_t start = output->Length;
    size_t i = 0;
    const size_t length = text.Length;
    bool inLineComment = false;
    bool inBlockComment = false;
    bool inString = false;
    bool inChar = false;

    while (i < length)
    {
        const char c = text.Data[i];
        const char c2 = i + 1 < length ? text.Data[i + 1] : '\0';

        if (inLineComment)
        {
            if (c == '\n')
            {
                inLineComment = false;
                Buffer_AppendChar(output, c);
            }

            i++;
            continue;
        }

        if (inBlockComment)
        {
            if (c == '*' && c2 == '/')
            {
                inBlockComment = false;
                i += 2;
                continue;
            }

            if (c == '\n')
            {
                Buffer_AppendChar(output, c);
            }

            i++;
            continue;
        }

        if (inString)
        {
            Buffer_AppendChar(output, c);

            if (c == '\\' && i + 1 < length)
            {
                Buffer_AppendChar(output, text.Data[i + 1]);
                i += 2;
                continue;
            }

            if (c == '"')
            {
                inString = false;
            }

            i++;
            continue;
        }

        if (inChar)
        {
            Buffer_AppendChar(output, c);

            if (c == '\\' && i + 1 < length)
            {
                Buffer_AppendChar(output, text.Data[i + 1]);
                i += 2;
                continue;
            }

            if (c == '\'')
            {
                inChar = false;
            }

            i++;
            continue;
        }

        if (c == '/' && c2 == '/')
        {
            inLineComment = true;
            i += 2;
            continue;
        }

        if (c == '/' && c2 == '*')
        {
            inBlockComment = true;
            i += 2;
            continue;
        }

        if (c == '"')
        {
            const size_t rawEnd = RawStringEnd(text, i);

            if (rawEnd > 0)
            {
                Buffer_Append(output, text.Data + i, rawEnd - i);
                i = rawEnd;
                continue;
            }

            inString = true;
        }
        else if (c == '\'' && !IsDigitSeparator(text, i))
        {
            inChar = true;
        }

        Buffer_AppendChar(output, c);
        i++;
    }

    DropDisabledCode(output, start);
}

static bool NextLine(Slice text, size_t *cursor, Buffer *line)
{
    if (*cursor > text.Length)
    {
        return false;
    }

    Buffer_Clear(line);

    while (*cursor < text.Length && text.Data[*cursor] != '\n')
    {
        if (text.Data[*cursor] != '\r')
        {
            Buffer_AppendChar(line, text.Data[*cursor]);
        }

        (*cursor)++;
    }

    (*cursor)++;
    return true;
}

/* A source file without its byte order mark, line continuations and comments */
static bool ReadSource(const char *path, Buffer *content)
{
    Buffer raw = {};
    Buffer spliced = {};
    const bool read = ReadWholeFile(path, &raw);
    Slice text = Buffer_Slice(&raw);

    if (text.Length >= 3 && memcmp(text.Data, "\xEF\xBB\xBF", 3) == 0)
    {
        text.Data += 3;
        text.Length -= 3;
    }

    SpliceContinuations(text, &spliced);
    StripComments(Buffer_Slice(&spliced), content);
    Buffer_Free(&spliced);
    Buffer_Free(&raw);
    return read;
}

typedef struct
{
    List Names;
    List Lines;
} Macros;

static void Macros_Set(Macros *macros, const char *name, const char *line)
{
    for (int i = 0; i < macros->Names.Count; i++)
    {
        if (strcmp(macros->Names.Items[i], name) == 0)
        {
            free(macros->Lines.Items[i]);
            const size_t length = strlen(line);
            macros->Lines.Items[i] = (char *)malloc(length + 1);
            memcpy(macros->Lines.Items[i], line, length + 1);
            return;
        }
    }

    List_Add(&macros->Names, name);
    List_Add(&macros->Lines, line);
}

static void Macros_Free(Macros *macros)
{
    List_Free(&macros->Names);
    List_Free(&macros->Lines);
}

static void GetMacros(Slice text, Macros *macros)
{
    Buffer line = {};
    size_t cursor = 0;

    while (NextLine(text, &cursor, &line))
    {
        const Slice trimmed = Slice_TrimStart(Buffer_Slice(&line));

        if (trimmed.Length == 0 || trimmed.Data[0] != '#')
        {
            continue;
        }

        const Slice rest = Slice_TrimStart(Slice{trimmed.Data + 1, trimmed.Length - 1});

        if (!Slice_StartsWithKeyword(rest, "define"))
        {
            continue;
        }

        const Slice afterDefine = Slice_TrimStart(Slice{rest.Data + 6, rest.Length - 6});
        size_t nameLength = 0;

        while (nameLength < afterDefine.Length && IsIdentChar(afterDefine.Data[nameLength]))
        {
            nameLength++;
        }

        if (nameLength == 0)
        {
            continue;
        }

        Buffer name = {};
        Buffer_Append(&name, afterDefine.Data, nameLength);

        const Slice whole = Slice_Trim(Buffer_Slice(&line));
        Buffer wholeText = {};
        Buffer_Append(&wholeText, whole.Data, whole.Length);

        Macros_Set(macros, name.Data, wholeText.Data);

        Buffer_Free(&name);
        Buffer_Free(&wholeText);
    }

    Buffer_Free(&line);
}

static void StripDirectives(Slice text, Buffer *output)
{
    Buffer line = {};
    size_t cursor = 0;
    bool first = true;

    while (NextLine(text, &cursor, &line))
    {
        if (!first)
        {
            Buffer_AppendChar(output, '\n');
        }

        first = false;

        const Slice trimmed = Slice_TrimStart(Buffer_Slice(&line));

        if (trimmed.Length > 0 && trimmed.Data[0] == '#')
        {
            continue;
        }

        Buffer_Append(output, line.Data, line.Length);
    }

    Buffer_Free(&line);
}

static void SplitTopLevelCommas(Slice text, List *parts)
{
    int bracketGroupCounter = 0;
    int angleCounter = 0;
    bool afterAssign = false;
    size_t start = 0;

    for (size_t i = 0; i < text.Length; i++)
    {
        const char c = text.Data[i];

        if (c == '(' || c == '[' || c == '{')
        {
            bracketGroupCounter++;
        }
        else if (c == ')' || c == ']' || c == '}')
        {
            bracketGroupCounter--;
        }
        else if (c == '<' && !afterAssign && bracketGroupCounter == 0 && i + 1 < text.Length && text.Data[i + 1] != '<' && text.Data[i + 1] != '=')
        {
            angleCounter++;
        }
        else if (c == '>' && !afterAssign && bracketGroupCounter == 0 && angleCounter > 0)
        {
            angleCounter--;
        }
        else if (c == '=' && bracketGroupCounter == 0 && angleCounter == 0)
        {
            const char previous = i > 0 ? text.Data[i - 1] : '\0';
            const char next = i + 1 < text.Length ? text.Data[i + 1] : '\0';

            if (next != '=' && previous != '=' && previous != '!' && previous != '<' && previous != '>')
            {
                afterAssign = true;
            }
        }
        else if (c == ',' && bracketGroupCounter == 0 && angleCounter == 0)
        {
            Buffer part = {};
            Buffer_Append(&part, text.Data + start, i - start);
            List_Add(parts, part.Data ? part.Data : "");
            Buffer_Free(&part);
            start = i + 1;
            afterAssign = false;
        }
    }

    Buffer part = {};
    Buffer_Append(&part, text.Data + start, text.Length - start);
    List_Add(parts, part.Data ? part.Data : "");
    Buffer_Free(&part);
}

static Slice SkipTemplateAndAttributes(Slice text);

static int FindTopLevelAssign(Slice text)
{
    int bracketGroupCounter = 0;

    const Slice afterTemplate = SkipTemplateAndAttributes(text);
    const size_t first = afterTemplate.Data >= text.Data && afterTemplate.Data <= text.Data + text.Length ? (size_t)(afterTemplate.Data - text.Data) : 0;

    for (size_t i = first; i < text.Length; i++)
    {
        const char c = text.Data[i];

        if (c == '(' || c == '[' || c == '{')
        {
            bracketGroupCounter++;
        }
        else if (c == ')' || c == ']' || c == '}')
        {
            bracketGroupCounter--;
        }
        else if (c == '=' && bracketGroupCounter == 0)
        {
            const char previous = i > 0 ? text.Data[i - 1] : '\0';
            const char next = i + 1 < text.Length ? text.Data[i + 1] : '\0';

            if (next != '=' && previous != '=' && previous != '!' && previous != '<' && previous != '>')
            {
                if (i >= 8 && strncmp(text.Data + i - 8, "operator", 8) == 0)
                {
                    continue;
                }

                return (int)i;
            }
        }
    }

    return -1;
}

static bool HasTopLevelParenthesis(Slice text)
{
    int angleCounter = 0;

    for (size_t i = 0; i < text.Length; i++)
    {
        if (text.Data[i] == '<')
        {
            angleCounter++;
        }
        else if (text.Data[i] == '>' && angleCounter > 0)
        {
            angleCounter--;
        }
        else if (text.Data[i] == '(' && angleCounter == 0)
        {
            return true;
        }
    }

    return false;
}

static bool ContainsWordSlice(Slice text, const char *word)
{
    Buffer copy = {};
    Buffer_Append(&copy, text.Data, text.Length);
    const bool found = copy.Data && ContainsWholeWord(copy.Data, word);
    Buffer_Free(&copy);
    return found;
}

static Slice SkipTemplateAndAttributes(Slice text)
{
    text = Slice_Trim(text);

    for (;;)
    {
        if (Slice_StartsWithKeyword(text, "template"))
        {
            size_t i = 8;
            int depth = 0;

            for (; i < text.Length; i++)
            {
                if (text.Data[i] == '<')
                {
                    depth++;
                }
                else if (text.Data[i] == '>' && --depth == 0)
                {
                    i++;
                    break;
                }
            }

            text = Slice_Trim(Slice{text.Data + i, text.Length - i});
            continue;
        }

        if (text.Length >= 2 && text.Data[0] == '[' && text.Data[1] == '[')
        {
            size_t i = 2;

            while (i + 1 < text.Length && !(text.Data[i] == ']' && text.Data[i + 1] == ']'))
            {
                i++;
            }

            i = i + 2 <= text.Length ? i + 2 : text.Length;
            text = Slice_Trim(Slice{text.Data + i, text.Length - i});
            continue;
        }

        return text;
    }
}

static Slice SkipSpecifiers(Slice text)
{
    static const char *specifiers[] = {"static", "inline", "constexpr", "consteval", "constinit", "extern", "thread_local", "typedef", "mutable", "volatile"};
    text = SkipTemplateAndAttributes(text);

    for (bool skipped = true; skipped;)
    {
        skipped = false;

        for (size_t s = 0; s < sizeof(specifiers) / sizeof(specifiers[0]); s++)
        {
            if (Slice_StartsWithKeyword(text, specifiers[s]))
            {
                const size_t length = strlen(specifiers[s]);
                text = SkipTemplateAndAttributes(Slice{text.Data + length, text.Length - length});
                skipped = true;
            }
        }
    }

    return text;
}

static bool IsClassLike(Slice head)
{
    if (HasTopLevelParenthesis(head) && !Slice_StartsWithKeyword(SkipSpecifiers(head), "enum"))
    {
        return false;
    }

    const Slice body = SkipSpecifiers(head);
    return Slice_StartsWithKeyword(body, "struct") || Slice_StartsWithKeyword(body, "class") || Slice_StartsWithKeyword(body, "union") || Slice_StartsWithKeyword(body, "enum");
}

static bool IsDeducedType(Slice text)
{
    const Slice body = SkipSpecifiers(text);

    if (!Slice_StartsWithKeyword(body, "auto") && !(body.Length >= 8 && strncmp(body.Data, "decltype", 8) == 0 && ContainsWordSlice(body, "auto")))
    {
        return false;
    }

    for (size_t i = 0; i + 1 < body.Length; i++)
    {
        if (body.Data[i] == '-' && body.Data[i + 1] == '>')
        {
            return false;
        }
    }

    return true;
}

static void StripDefaultArguments(const char *line, Buffer *output)
{
    const size_t length = strlen(line);
    size_t i = 0;
    int depth = 0;

    while (i < length)
    {
        const char c = line[i];

        if (c == '(' && depth == 0)
        {
            int inner = 0;
            size_t close = i + 1;

            for (; close < length; close++)
            {
                if (line[close] == '(' || line[close] == '[' || line[close] == '{')
                {
                    inner++;
                }
                else if (line[close] == ')' || line[close] == ']' || line[close] == '}')
                {
                    if (inner == 0)
                    {
                        break;
                    }

                    inner--;
                }
            }

            List parameters = {};
            SplitTopLevelCommas(Slice{line + i + 1, close - i - 1}, &parameters);
            Buffer_AppendChar(output, '(');

            for (int p = 0; p < parameters.Count; p++)
            {
                Slice parameter = {parameters.Items[p], strlen(parameters.Items[p])};
                const int assign = FindTopLevelAssign(parameter);

                if (assign >= 0)
                {
                    parameter = Slice_TrimEnd(Slice{parameter.Data, (size_t)assign});
                }

                if (p > 0)
                {
                    Buffer_AppendChar(output, ',');
                }

                Buffer_Append(output, parameter.Data, parameter.Length);
            }

            Buffer_AppendChar(output, ')');
            List_Free(&parameters);
            i = close + 1;
            continue;
        }

        if (c == '[' || c == '{')
        {
            depth++;
        }
        else if (c == ']' || c == '}')
        {
            depth--;
        }

        Buffer_AppendChar(output, c);
        i++;
    }
}

static int OpeningBracket(Slice slice, char open, char close);

/* Rng(1234): parentheses holding values (a direct initialization), not parameters */
static bool IsDirectInitializer(Slice inside)
{
    List arguments = {};
    bool values = false;
    bool parameters = false;
    SplitTopLevelCommas(inside, &arguments);

    for (int i = 0; i < arguments.Count; i++)
    {
        const Slice argument = Slice_Trim(Slice{arguments.Items[i], strlen(arguments.Items[i])});
        const char first = argument.Length > 0 ? argument.Data[0] : '\0';
        const char *dot = (const char *)memchr(argument.Data, '.', argument.Length);

        if (argument.Length == 0 || FindTopLevelAssign(argument) >= 0)
        {
            parameters = true;
        }
        else if (isdigit((unsigned char)first) || strchr("'\"-+!~(&{[", first) || Slice_StartsWithKeyword(argument, "true") || Slice_StartsWithKeyword(argument, "false") ||
                 Slice_StartsWithKeyword(argument, "nullptr") || Slice_StartsWithKeyword(argument, "sizeof") || memchr(argument.Data, '"', argument.Length) ||
                 (dot && !(dot + 2 < argument.Data + argument.Length && dot[1] == '.' && dot[2] == '.')) || strstr(arguments.Items[i], "->"))
        {
            values = true;
        }
    }

    List_Free(&arguments);
    return values && !parameters;
}

static bool Classify(Slice rawCandidate, Buffer *declaration)
{
    Slice candidate = Slice_Trim(rawCandidate);

    if (candidate.Length == 0 || Slice_IsBareIdentifier(candidate))
    {
        return false;
    }

    Slice attributes = {candidate.Data, 0};

    for (;;)
    {
        if (candidate.Length >= 2 && candidate.Data[0] == '[' && candidate.Data[1] == '[')
        {
            const Slice rest = SkipTemplateAndAttributes(candidate);
            attributes.Length = (size_t)(rest.Data - attributes.Data);
            candidate = rest;
            continue;
        }

        if (Slice_StartsWithKeyword(candidate, "alignas") || Slice_StartsWithKeyword(candidate, "_Alignas"))
        {
            const char *open = (const char *)memchr(candidate.Data, '(', candidate.Length);
            int depth = 0;
            size_t i = open ? (size_t)(open - candidate.Data) : candidate.Length;

            for (; i < candidate.Length; i++)
            {
                if (candidate.Data[i] == '(')
                {
                    depth++;
                }
                else if (candidate.Data[i] == ')' && --depth == 0)
                {
                    i++;
                    break;
                }
            }

            const Slice rest = Slice_Trim(Slice{candidate.Data + i, candidate.Length - i});
            attributes.Length = (size_t)(rest.Data - attributes.Data);
            candidate = rest;
            continue;
        }

        break;
    }

    List segments = {};
    SplitTopLevelCommas(candidate, &segments);

    List cleanedSegments = {};

    for (int i = 0; i < segments.Count; i++)
    {
        Slice segment = Slice_Trim(Slice{segments.Items[i], strlen(segments.Items[i])});
        const int assignIndex = FindTopLevelAssign(segment);

        if (assignIndex >= 0)
        {
            segment = Slice_Trim(Slice{segment.Data, (size_t)assignIndex});
        }
        else if (segment.Length > 0 && segment.Data[segment.Length - 1] == ')')
        {
            const int open = OpeningBracket(segment, '(', ')');

            if (open > 0 && IsDirectInitializer(Slice{segment.Data + open + 1, segment.Length - (size_t)open - 2}))
            {
                segment = Slice_Trim(Slice{segment.Data, (size_t)open});
            }
        }

        if (segment.Length > 0)
        {
            Buffer cleaned = {};
            Buffer_Append(&cleaned, segment.Data, segment.Length);
            List_Add(&cleanedSegments, cleaned.Data);
            Buffer_Free(&cleaned);
        }
    }

    bool produced = false;

    if (cleanedSegments.Count > 0)
    {
        if (attributes.Length > 0)
        {
            Buffer_Append(declaration, attributes.Data, attributes.Length);
            Buffer_AppendChar(declaration, ' ');
        }

        Buffer_AppendText(declaration, "extern ");

        for (int i = 0; i < cleanedSegments.Count; i++)
        {
            if (i > 0)
            {
                Buffer_AppendText(declaration, ", ");
            }

            Buffer_AppendText(declaration, cleanedSegments.Items[i]);
        }

        Buffer_AppendChar(declaration, ';');
        produced = true;
    }

    List_Free(&segments);
    List_Free(&cleanedSegments);
    return produced;
}

static bool IsMacroInvocation(Slice statement, const Macros *macros)
{
    statement = Slice_Trim(statement);
    size_t cursor = 0;

    while (cursor < statement.Length && IsIdentChar(statement.Data[cursor]))
    {
        cursor++;
    }

    if (cursor == 0 || !IsIdentStartChar(statement.Data[0]))
    {
        return false;
    }

    char name[256];
    const size_t nameLength = cursor < sizeof(name) ? cursor : sizeof(name) - 1;
    memcpy(name, statement.Data, nameLength);
    name[nameLength] = '\0';

    while (cursor < statement.Length && IsWhitespace(statement.Data[cursor]))
    {
        cursor++;
    }

    if (cursor >= statement.Length || statement.Data[cursor] != '(')
    {
        return false;
    }

    int depth = 0;

    for (; cursor < statement.Length; cursor++)
    {
        if (statement.Data[cursor] == '(')
        {
            depth++;
        }
        else if (statement.Data[cursor] == ')' && --depth == 0)
        {
            break;
        }
    }

    if (depth != 0 || cursor + 1 != statement.Length)
    {
        return false;
    }

    for (int m = 0; m < macros->Names.Count; m++)
    {
        if (strcmp(macros->Names.Items[m], name) == 0)
        {
            char pattern[300];
            snprintf(pattern, sizeof(pattern), "%s(", name);
            return strstr(macros->Lines.Items[m], pattern) != NULL;
        }
    }

    bool hasLetter = false;

    for (const char *character = name; *character; character++)
    {
        if (*character >= 'a' && *character <= 'z')
        {
            return false;
        }

        hasLetter |= (*character >= 'A' && *character <= 'Z');
    }

    return hasLetter;
}

static void ResolveMacroClosure(const Macros *macros, const List *initialNames, bool *needed)
{
    List queue = {};
    int head = 0;

    for (int i = 0; i < initialNames->Count; i++)
    {
        for (int m = 0; m < macros->Names.Count; m++)
        {
            if (strcmp(macros->Names.Items[m], initialNames->Items[i]) == 0 && !needed[m])
            {
                needed[m] = true;
                List_Add(&queue, macros->Names.Items[m]);
            }
        }
    }

    while (head < queue.Count)
    {
        const char *current = queue.Items[head++];
        const char *definition = NULL;

        for (int m = 0; m < macros->Names.Count; m++)
        {
            if (strcmp(macros->Names.Items[m], current) == 0)
            {
                definition = macros->Lines.Items[m];
                break;
            }
        }

        for (int m = 0; m < macros->Names.Count; m++)
        {
            if (strcmp(macros->Names.Items[m], current) == 0)
            {
                continue;
            }

            if (ContainsWholeWord(definition, macros->Names.Items[m]) && !needed[m])
            {
                needed[m] = true;
                List_Add(&queue, macros->Names.Items[m]);
            }
        }
    }

    List_Free(&queue);
}

static void AppendLine(Buffer *output, const char *line)
{
    for (const char *cursor = line; *cursor; cursor++)
    {
        Buffer_AppendChar(output, (unsigned char)*cursor > 127 ? '?' : *cursor);
    }

    Buffer_AppendText(output, "\r\n");
}

static void AppendSource(Buffer *output, const char *text)
{
    for (const char *cursor = text; *cursor; cursor++)
    {
        if (*cursor == '\r')
        {
            continue;
        }

        if (*cursor == '\n')
        {
            Buffer_AppendText(output, "\r\n");
            continue;
        }

        Buffer_AppendChar(output, *cursor);
    }

    Buffer_AppendText(output, "\r\n");
}

static bool IsTypeKeyword(const char *word)
{
    static const char *keywords[] = {"void", "char", "short", "int", "long", "float", "double", "signed", "unsigned", "const", "volatile",
                                     "struct", "union", "enum", "class", "static", "extern", "inline", "restrict", "_Bool", "bool", "typedef",
                                     "auto", "constexpr", "consteval", "constinit", "noexcept", "override", "final", "operator", "decltype"};

    for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++)
    {
        if (strcmp(word, keywords[i]) == 0)
        {
            return true;
        }
    }

    return false;
}

static bool LastIdentifier(Slice slice, Buffer *name)
{
    slice = Slice_Trim(slice);
    size_t start = slice.Length;

    while (start > 0 && IsIdentChar(slice.Data[start - 1]))
    {
        start--;
    }

    if (start == slice.Length || !IsIdentStartChar(slice.Data[start]))
    {
        return false;
    }

    if (start >= 2 && slice.Data[start - 1] == ':' && slice.Data[start - 2] == ':')
    {
        return false;
    }

    Buffer_Clear(name);
    Buffer_Append(name, slice.Data + start, slice.Length - start);
    return true;
}

static int OpeningBracket(Slice slice, char open, char close)
{
    int depth = 0;

    for (size_t i = slice.Length; i > 0; i--)
    {
        const char c = slice.Data[i - 1];

        if (c == close)
        {
            depth++;
        }
        else if (c == open && --depth == 0)
        {
            return (int)(i - 1);
        }
    }

    return -1;
}

static bool DeclaratorName(Slice declarator, Buffer *name)
{
    declarator = Slice_Trim(declarator);

    for (bool trimmed = true; trimmed && declarator.Length > 0;)
    {
        trimmed = false;
        const char last = declarator.Data[declarator.Length - 1];

        if (IsIdentChar(last) || last == '>' || last == '*' || last == '&')
        {
            int arrow = -1;

            for (size_t i = 0; i + 1 < declarator.Length; i++)
            {
                if (declarator.Data[i] == '-' && declarator.Data[i + 1] == '>')
                {
                    arrow = (int)i;
                }
            }

            if (arrow > 0)
            {
                declarator = Slice_TrimEnd(Slice{declarator.Data, (size_t)arrow});
                trimmed = true;
                continue;
            }

            Buffer word = {};

            if (LastIdentifier(declarator, &word) && (strcmp(word.Data, "const") == 0 || strcmp(word.Data, "noexcept") == 0 || strcmp(word.Data, "override") == 0 || strcmp(word.Data, "final") == 0))
            {
                declarator = Slice_TrimEnd(Slice{declarator.Data, declarator.Length - word.Length});
                trimmed = true;
            }

            Buffer_Free(&word);
        }
    }

    while (declarator.Length > 0 && declarator.Data[declarator.Length - 1] == ']')
    {
        const int open = OpeningBracket(declarator, '[', ']');

        if (open < 0)
        {
            return false;
        }

        declarator = Slice_TrimEnd(Slice{declarator.Data, (size_t)open});
    }

    if (declarator.Length > 0 && declarator.Data[declarator.Length - 1] == ')')
    {
        int parameters = OpeningBracket(declarator, '(', ')');

        if (parameters < 0)
        {
            return false;
        }

        Slice before = Slice_TrimEnd(Slice{declarator.Data, (size_t)parameters});

        Buffer word = {};

        if (LastIdentifier(before, &word) && strcmp(word.Data, "noexcept") == 0)
        {
            Buffer_Free(&word);
            return DeclaratorName(Slice_TrimEnd(Slice{before.Data, before.Length - 8}), name);
        }

        Buffer_Free(&word);

        if (before.Length > 0 && before.Data[before.Length - 1] == ')')
        {
            const int group = OpeningBracket(before, '(', ')');

            if (group < 0)
            {
                return false;
            }

            Slice inside = {before.Data + group + 1, before.Length - (size_t)group - 2};
            inside = Slice_Trim(inside);

            while (inside.Length > 0 && (inside.Data[0] == '*' || inside.Data[0] == '&' || IsWhitespace(inside.Data[0])))
            {
                inside.Data++;
                inside.Length--;
            }

            return DeclaratorName(inside, name);
        }

        return LastIdentifier(before, name);
    }

    return LastIdentifier(declarator, name);
}

static bool DeclarationNames(Slice text, List *names)
{
    text = Slice_Trim(text);

    if (text.Length > 0 && text.Data[text.Length - 1] == ';')
    {
        text.Length--;
    }

    text = SkipSpecifiers(text);

    List parts = {};
    SplitTopLevelCommas(text, &parts);
    Buffer name = {};
    bool found = parts.Count > 0;

    for (int i = 0; i < parts.Count && found; i++)
    {
        Slice part = Slice_Trim(Slice{parts.Items[i], strlen(parts.Items[i])});
        const int assign = FindTopLevelAssign(part);

        if (assign >= 0)
        {
            part = Slice_Trim(Slice{part.Data, (size_t)assign});
        }

        found = DeclaratorName(part, &name) && !IsTypeKeyword(name.Data);

        if (found)
        {
            List_Add(names, name.Data);
        }
    }

    Buffer_Free(&name);
    List_Free(&parts);
    return found;
}

static bool ClassTagName(Slice head, Buffer *name)
{
    Slice text = SkipSpecifiers(head);
    static const char *keywords[] = {"struct", "class", "union", "enum"};

    for (size_t k = 0; k < sizeof(keywords) / sizeof(keywords[0]); k++)
    {
        if (Slice_StartsWithKeyword(text, keywords[k]))
        {
            const size_t length = strlen(keywords[k]);
            text = Slice_Trim(Slice{text.Data + length, text.Length - length});
            break;
        }
    }

    if (Slice_StartsWithKeyword(text, "class") || Slice_StartsWithKeyword(text, "struct"))
    {
        text = Slice_Trim(Slice{text.Data + (text.Data[0] == 'c' ? 5 : 6), text.Length - (text.Data[0] == 'c' ? 5 : 6)});
    }

    text = SkipTemplateAndAttributes(text);

    while (Slice_StartsWithKeyword(text, "alignas") || Slice_StartsWithKeyword(text, "__declspec"))
    {
        const char *open = (const char *)memchr(text.Data, '(', text.Length);

        if (!open)
        {
            return false;
        }

        Slice rest = {open, text.Length - (size_t)(open - text.Data)};
        int depth = 0;
        size_t i = 0;

        for (; i < rest.Length; i++)
        {
            if (rest.Data[i] == '(')
            {
                depth++;
            }
            else if (rest.Data[i] == ')' && --depth == 0)
            {
                break;
            }
        }

        text = Slice_Trim(Slice{rest.Data + i + 1, rest.Length > i + 1 ? rest.Length - i - 1 : 0});
    }

    size_t length = 0;

    while (length < text.Length && IsIdentChar(text.Data[length]))
    {
        length++;
    }

    if (length == 0 || !IsIdentStartChar(text.Data[0]))
    {
        return false;
    }

    Buffer_Clear(name);
    Buffer_Append(name, text.Data, length);
    return !IsTypeKeyword(name->Data);
}

static void EnumeratorNames(Slice body, List *names)
{
    List parts = {};
    SplitTopLevelCommas(body, &parts);

    for (int i = 0; i < parts.Count; i++)
    {
        Slice part = Slice_Trim(Slice{parts.Items[i], strlen(parts.Items[i])});
        size_t length = 0;

        while (length < part.Length && IsIdentChar(part.Data[length]))
        {
            length++;
        }

        if (length > 0 && IsIdentStartChar(part.Data[0]))
        {
            Buffer name = {};
            Buffer_Append(&name, part.Data, length);
            List_Add(names, name.Data);
            Buffer_Free(&name);
        }
    }

    List_Free(&parts);
}

typedef struct
{
    Buffer Open;
    Buffer Close;
    int Depth;
    int PrivateDepth;
    size_t OpenLengths[64];
    size_t CloseLengths[64];
    bool Private[64];
} Namespaces;

static void Namespaces_PushText(Namespaces *namespaces, const char *open, const char *close, bool isPrivate)
{
    if (namespaces->Depth >= 64)
    {
        return;
    }

    namespaces->OpenLengths[namespaces->Depth] = namespaces->Open.Length;
    namespaces->CloseLengths[namespaces->Depth] = namespaces->Close.Length;
    namespaces->Private[namespaces->Depth] = isPrivate;
    namespaces->Depth++;
    namespaces->PrivateDepth += isPrivate ? 1 : 0;
    Buffer_AppendText(&namespaces->Open, open);
    Buffer_AppendText(&namespaces->Close, close);
}

static void Namespaces_Push(Namespaces *namespaces, Slice name)
{
    if (name.Length == 0)
    {
        Namespaces_PushText(namespaces, "namespace { ", " }", true);
        return;
    }

    Buffer open = {};
    Buffer_AppendText(&open, "namespace ");
    Buffer_Append(&open, name.Data, name.Length);
    Buffer_AppendText(&open, " { ");
    Namespaces_PushText(namespaces, open.Data, " }", false);
    Buffer_Free(&open);
}

static void Namespaces_Pop(Namespaces *namespaces)
{
    if (namespaces->Depth == 0)
    {
        return;
    }

    namespaces->Depth--;
    namespaces->PrivateDepth -= namespaces->Private[namespaces->Depth] ? 1 : 0;
    namespaces->Open.Length = namespaces->OpenLengths[namespaces->Depth];
    namespaces->Close.Length = namespaces->CloseLengths[namespaces->Depth];

    if (namespaces->Open.Data)
    {
        namespaces->Open.Data[namespaces->Open.Length] = '\0';
    }

    if (namespaces->Close.Data)
    {
        namespaces->Close.Data[namespaces->Close.Length] = '\0';
    }
}

static void Namespaces_Free(Namespaces *namespaces)
{
    Buffer_Free(&namespaces->Open);
    Buffer_Free(&namespaces->Close);
}

static const char *Namespaces_Key(const Namespaces *namespaces)
{
    return namespaces->Open.Data ? namespaces->Open.Data : "";
}

static void Namespaces_Wrap(const Namespaces *namespaces, const char *line, Buffer *output)
{
    if (namespaces->Depth > 0)
    {
        Buffer_Append(output, namespaces->Open.Data, namespaces->Open.Length);
    }

    Buffer_AppendText(output, line);

    if (namespaces->Depth > 0)
    {
        Buffer_Append(output, namespaces->Close.Data, namespaces->Close.Length);
    }
}

typedef struct
{
    List Kinds;
    List Lines;
    List OwnerLines;
    List Spaces;
    List Names;
} Items;

static void Items_Free(Items *items)
{
    List_Free(&items->Kinds);
    List_Free(&items->Lines);
    List_Free(&items->OwnerLines);
    List_Free(&items->Spaces);
    List_Free(&items->Names);
}

static void JoinNames(const List *names, Buffer *joined)
{
    for (int i = 0; i < names->Count; i++)
    {
        if (i > 0)
        {
            Buffer_AppendChar(joined, ',');
        }

        Buffer_AppendText(joined, names->Items[i]);
    }
}

static void Items_Add(Items *items, char kind, const char *line, const char *ownerLine, const Namespaces *namespaces, const List *names)
{
    Buffer wrapped = {};
    Buffer ownerWrapped = {};
    Buffer joined = {};
    const char kindText[2] = {kind, '\0'};

    Namespaces_Wrap(namespaces, line, &wrapped);
    Namespaces_Wrap(namespaces, ownerLine, &ownerWrapped);
    JoinNames(names, &joined);

    bool repeated = false;

    for (int i = 0; i < items->Lines.Count && !repeated; i++)
    {
        repeated = items->Kinds.Items[i][0] == kind && strcmp(items->Lines.Items[i], wrapped.Data) == 0;
    }

    if (!repeated)
    {
        List_Add(&items->Kinds, kindText);
        List_Add(&items->Lines, wrapped.Data);
        List_Add(&items->OwnerLines, ownerWrapped.Data);
        List_Add(&items->Spaces, Namespaces_Key(namespaces));
        List_Add(&items->Names, joined.Data ? joined.Data : "");
    }

    Buffer_Free(&wrapped);
    Buffer_Free(&ownerWrapped);
    Buffer_Free(&joined);
}

static bool Items_HasVariable(const Items *items, const Namespaces *namespaces, const List *names)
{
    for (int i = 0; i < items->Lines.Count; i++)
    {
        if (items->Kinds.Items[i][0] != 'P' || strcmp(items->Spaces.Items[i], Namespaces_Key(namespaces)) != 0)
        {
            continue;
        }

        for (int n = 0; n < names->Count; n++)
        {
            if (strcmp(items->Names.Items[i], names->Items[n]) == 0)
            {
                return true;
            }
        }
    }

    return false;
}

static void RawText(Slice scanContent, size_t start, size_t end, Buffer *text)
{
    Slice raw = Slice_Trim(Slice{scanContent.Data + start, end > start ? end - start : 0});
    Buffer_Append(text, raw.Data, raw.Length);
}

static bool IsSpecialization(Slice text)
{
    text = Slice_Trim(text);

    if (!Slice_StartsWithKeyword(text, "template"))
    {
        return false;
    }

    const Slice rest = Slice_TrimStart(Slice{text.Data + 8, text.Length - 8});

    if (rest.Length < 2 || rest.Data[0] != '<')
    {
        return false;
    }

    const Slice inside = Slice_TrimStart(Slice{rest.Data + 1, rest.Length - 1});
    return inside.Length > 0 && inside.Data[0] == '>';
}

static bool IsQualified(Slice text)
{
    text = SkipSpecifiers(text);
    int angleCounter = 0;
    size_t end = text.Length;

    for (size_t i = 0; i < text.Length; i++)
    {
        if (text.Data[i] == '<')
        {
            angleCounter++;
        }
        else if (text.Data[i] == '>' && angleCounter > 0)
        {
            angleCounter--;
        }
        else if ((text.Data[i] == '(' || text.Data[i] == '=' || text.Data[i] == '[' || text.Data[i] == '{') && angleCounter == 0)
        {
            end = i;
            break;
        }
    }

    Slice head = Slice_TrimEnd(Slice{text.Data, end});
    size_t start = head.Length;

    while (start > 0 && !IsWhitespace(head.Data[start - 1]) && head.Data[start - 1] != '*' && head.Data[start - 1] != '&' && head.Data[start - 1] != '>')
    {
        start--;
    }

    for (size_t i = start; i + 1 < head.Length; i++)
    {
        if (head.Data[i] == ':' && head.Data[i + 1] == ':')
        {
            return true;
        }
    }

    return false;
}

static List PrivateNames = {};
static const char *ScanOwner = "";

static void AddPrivateNames(const List *names)
{
    for (int i = 0; i < names->Count; i++)
    {
        char key[MAX_PATH_LENGTH + 256];
        snprintf(key, sizeof(key), "%s|%s", ScanOwner, names->Items[i]);
        List_Add(&PrivateNames, key);
    }
}

static void AddStatement(Slice declaration, Slice raw, const Namespaces *namespaces, Items *items)
{
    declaration = Slice_Trim(declaration);

    if (declaration.Length == 0 || Slice_IsBareIdentifier(declaration))
    {
        return;
    }

    Buffer rawText = {};
    Buffer_Append(&rawText, raw.Data, raw.Length);
    List names = {};

    if (Slice_StartsWithKeyword(declaration, "typedef"))
    {
        if (DeclarationNames(declaration, &names) && namespaces->PrivateDepth == 0)
        {
            Items_Add(items, 'T', rawText.Data, rawText.Data, namespaces, &names);
        }
    }
    else if (Slice_StartsWithKeyword(declaration, "using"))
    {
        const Slice rest = Slice_Trim(Slice{declaration.Data + 5, declaration.Length - 5});
        const int assign = FindTopLevelAssign(rest);

        if (assign > 0 && !Slice_StartsWithKeyword(rest, "namespace"))
        {
            Buffer name = {};

            if (LastIdentifier(Slice{rest.Data, (size_t)assign}, &name))
            {
                List_Add(&names, name.Data);

                if (namespaces->PrivateDepth == 0)
                {
                    Items_Add(items, 'T', rawText.Data, rawText.Data, namespaces, &names);
                }
            }

            Buffer_Free(&name);
        }
    }
    else if (Slice_StartsWithKeyword(declaration, "extern") && raw.Length > 8 && strncmp(Slice_Trim(raw).Data, "extern \"C\"", 10) == 0)
    {
        const Slice rest = Slice_Trim(Slice{declaration.Data + 6, declaration.Length - 6});

        if (DeclarationNames(rest, &names) && namespaces->PrivateDepth == 0)
        {
            Items_Add(items, 'P', rawText.Data, rawText.Data, namespaces, &names);
        }
    }
    else if (Slice_StartsWithKeyword(declaration, "static_assert") || Slice_StartsWithKeyword(declaration, "_Static_assert") ||
             Slice_StartsWithKeyword(declaration, "namespace") || Slice_StartsWithKeyword(declaration, "extern") ||
             Slice_StartsWithKeyword(declaration, "friend"))
    {
    }
    else if (Slice_StartsWithKeyword(SkipTemplateAndAttributes(declaration), "struct") || Slice_StartsWithKeyword(SkipTemplateAndAttributes(declaration), "class") ||
             Slice_StartsWithKeyword(SkipTemplateAndAttributes(declaration), "union") || Slice_StartsWithKeyword(SkipTemplateAndAttributes(declaration), "enum"))
    {
        if (!HasTopLevelParenthesis(declaration) && IsClassLike(declaration) && ContainsWordSlice(declaration, "struct") + ContainsWordSlice(declaration, "class") + ContainsWordSlice(declaration, "union") + ContainsWordSlice(declaration, "enum") > 0)
        {
            Buffer tag = {};

            if (ClassTagName(declaration, &tag) && declaration.Length >= tag.Length && memcmp(declaration.Data + (declaration.Length - tag.Length), tag.Data, tag.Length) == 0 && (declaration.Length == tag.Length || !IsIdentChar(declaration.Data[declaration.Length - tag.Length - 1])))
            {
                List_Add(&names, tag.Data);

                if (namespaces->PrivateDepth == 0)
                {
                    Items_Add(items, 'T', rawText.Data, rawText.Data, namespaces, &names);
                }

                Buffer_Free(&tag);
                List_Free(&names);
                Buffer_Free(&rawText);
                return;
            }

            Buffer_Free(&tag);
        }

        Buffer declarationText = {};

        if (DeclarationNames(declaration, &names) && namespaces->PrivateDepth == 0 && !Slice_StartsWithKeyword(declaration, "static") && Classify(declaration, &declarationText))
        {
            Items_Add(items, 'P', declarationText.Data, declarationText.Data, namespaces, &names);
        }

        Buffer_Free(&declarationText);
    }
    else
    {
        const Slice body = SkipTemplateAndAttributes(declaration);
        const bool isTemplate = Slice_StartsWithKeyword(declaration, "template");
        const bool isInline = ContainsWordSlice(body, "inline") || ContainsWordSlice(body, "constexpr") || ContainsWordSlice(body, "consteval");
        const bool isStatic = Slice_StartsWithKeyword(body, "static");
        const bool hasNames = DeclarationNames(declaration, &names);
        const bool isPrototype = HasTopLevelParenthesis(declaration) && FindTopLevelAssign(declaration) < 0;

        if (namespaces->PrivateDepth > 0 || isStatic || IsDeducedType(declaration) || IsSpecialization(declaration) || IsQualified(declaration))
        {
            AddPrivateNames(&names);
        }
        else if (isTemplate || isInline)
        {
            if (hasNames && !isPrototype)
            {
                Items_Add(items, 'F', rawText.Data, rawText.Data, namespaces, &names);
            }
        }
        else
        {
            Buffer declarationText = {};

            if (hasNames && !HasTopLevelParenthesis(declaration) && Items_HasVariable(items, namespaces, &names))
            {
            }
            else if ((hasNames || ContainsWordSlice(declaration, "operator")) && Classify(declaration, &declarationText))
            {
                Buffer ownerText = {};
                StripDefaultArguments(declarationText.Data, &ownerText);
                Items_Add(items, hasNames ? 'P' : 'A', declarationText.Data, ownerText.Data, namespaces, &names);
                Buffer_Free(&ownerText);
            }

            Buffer_Free(&declarationText);
        }
    }

    List_Free(&names);
    Buffer_Free(&rawText);
}

static void AddFunction(Slice signature, const Namespaces *namespaces, Items *items)
{
    List names = {};
    const bool hasNames = DeclarationNames(signature, &names);
    const Slice body = SkipTemplateAndAttributes(signature);

    if (namespaces->PrivateDepth > 0 || Slice_StartsWithKeyword(body, "static") || IsDeducedType(signature) || IsSpecialization(signature) || IsQualified(signature))
    {
        AddPrivateNames(&names);
        List_Free(&names);
        return;
    }

    if (Slice_StartsWithKeyword(signature, "template") || ContainsWordSlice(body, "inline") || ContainsWordSlice(body, "constexpr") || ContainsWordSlice(body, "consteval"))
    {
        if (hasNames)
        {
            Buffer text = {};
            Buffer_Append(&text, signature.Data, signature.Length);
            Items_Add(items, 'I', text.Data, text.Data, namespaces, &names);
            Buffer_Free(&text);
        }

        List_Free(&names);
        return;
    }

    Buffer declaration = {};

    if ((hasNames || ContainsWordSlice(signature, "operator")) && Classify(signature, &declaration))
    {
        Buffer ownerText = {};
        StripDefaultArguments(declaration.Data, &ownerText);
        Items_Add(items, hasNames ? 'P' : 'A', declaration.Data, ownerText.Data, namespaces, &names);
        Buffer_Free(&ownerText);
    }

    Buffer_Free(&declaration);
    List_Free(&names);
}

static void ClassForward(Slice head, Slice aliases, Buffer *forward, List *covered)
{
    head = Slice_Trim(head);

    if (Slice_StartsWithKeyword(head, "typedef"))
    {
        head = Slice_Trim(Slice{head.Data + 7, head.Length - 7});
    }

    Buffer tag = {};

    if (Slice_StartsWithKeyword(head, "template") || !ClassTagName(head, &tag))
    {
        Buffer_Free(&tag);
        return;
    }

    const Slice body = SkipSpecifiers(head);
    const char *keyword = Slice_StartsWithKeyword(body, "struct") ? "struct" : Slice_StartsWithKeyword(body, "class") ? "class" : Slice_StartsWithKeyword(body, "union") ? "union" : "enum";

    if (strcmp(keyword, "enum") == 0)
    {
        const Slice afterEnum = Slice_Trim(Slice{body.Data + 4, body.Length - 4});
        const bool scoped = Slice_StartsWithKeyword(afterEnum, "class") || Slice_StartsWithKeyword(afterEnum, "struct");
        const char *colon = NULL;

        for (size_t i = 0; i < body.Length; i++)
        {
            if (body.Data[i] == ':' && (i + 1 >= body.Length || body.Data[i + 1] != ':') && (i == 0 || body.Data[i - 1] != ':'))
            {
                colon = body.Data + i;
                break;
            }
        }

        if (!scoped && !colon)
        {
            Buffer_Free(&tag);
            return;
        }

        Buffer_AppendText(forward, scoped ? "enum class " : "enum ");
        Buffer_AppendText(forward, tag.Data);

        if (colon)
        {
            Buffer_AppendText(forward, " ");
            Buffer_Append(forward, colon, (size_t)(body.Data + body.Length - colon));
        }

        Buffer_AppendChar(forward, ';');
        List_Add(covered, tag.Data);
        Buffer_Free(&tag);
        return;
    }

    Buffer_AppendText(forward, keyword);
    Buffer_AppendChar(forward, ' ');
    Buffer_AppendText(forward, tag.Data);
    Buffer_AppendChar(forward, ';');
    List_Add(covered, tag.Data);

    List parts = {};
    SplitTopLevelCommas(aliases, &parts);

    for (int p = 0; p < parts.Count && aliases.Length > 0; p++)
    {
        const Slice alias = Slice_Trim(Slice{parts.Items[p], strlen(parts.Items[p])});

        if (Slice_IsBareIdentifier(alias))
        {
            Buffer_AppendText(forward, " typedef ");
            Buffer_AppendText(forward, keyword);
            Buffer_AppendChar(forward, ' ');
            Buffer_AppendText(forward, tag.Data);
            Buffer_AppendChar(forward, ' ');
            Buffer_Append(forward, alias.Data, alias.Length);
            Buffer_AppendChar(forward, ';');

            Buffer name = {};
            Buffer_Append(&name, alias.Data, alias.Length);
            List_Add(covered, name.Data);
            Buffer_Free(&name);
        }
    }

    List_Free(&parts);
    Buffer_Free(&tag);
}

enum
{
    PENDING_NONE,
    PENDING_VARIABLE,
    PENDING_CLASS
};

static void ScanDeclarations(Slice scanContent, const Macros *macros, Items *items)
{
    Buffer buffer = {};
    Buffer pendingHead = {};
    int pending = PENDING_NONE;
    size_t statementStart = 0;
    size_t bodyStart = 0;
    Namespaces namespaces = {};

    int curlyGroupCounter = 0;
    int parenGroupCounter = 0;

    bool inString = false;
    bool inChar = false;

    for (size_t i = 0; i < scanContent.Length; i++)
    {
        const char c = scanContent.Data[i];

        if (c == '\n')
        {
            inString = false;
            inChar = false;

            if (curlyGroupCounter == 0)
            {
                if (IsMacroInvocation(Buffer_Slice(&buffer), macros))
                {
                    Buffer_Clear(&buffer);
                    statementStart = i + 1;
                }
                else if (buffer.Length > 0)
                {
                    Buffer_AppendChar(&buffer, ' ');
                }
            }

            continue;
        }

        const bool keepLiteral = curlyGroupCounter == 0 && parenGroupCounter > 0;

        if (inString || inChar)
        {
            if (c == '\\' && i + 1 < scanContent.Length)
            {
                if (keepLiteral)
                {
                    Buffer_Append(&buffer, scanContent.Data + i, 2);
                }

                i++;
                continue;
            }

            const bool closing = (inString && c == '"') || (inChar && c == '\'');
            inString = inString && !closing;
            inChar = inChar && !closing;

            if (keepLiteral && (closing || !strchr(",;=(){}[]<>", c)) && (unsigned char)c < 128)
            {
                Buffer_AppendChar(&buffer, c);
            }
            else if (keepLiteral)
            {
                char escaped[8];
                snprintf(escaped, sizeof(escaped), "\\%03o", (unsigned char)c);
                Buffer_AppendText(&buffer, escaped);
            }

            continue;
        }

        if (c == '"')
        {
            const size_t rawEnd = RawStringEnd(scanContent, i);

            if (rawEnd > 0)
            {
                i = rawEnd - 1;
                continue;
            }

            inString = true;

            if (keepLiteral)
            {
                Buffer_AppendChar(&buffer, c);
            }

            continue;
        }

        if (c == '\'' && !IsDigitSeparator(scanContent, i))
        {
            inChar = true;

            if (keepLiteral)
            {
                Buffer_AppendChar(&buffer, c);
            }

            continue;
        }

        if (curlyGroupCounter == 0)
        {
            if (c == '(')
            {
                parenGroupCounter++;
            }
            else if (c == ')' && parenGroupCounter > 0)
            {
                parenGroupCounter--;
            }
            else if (parenGroupCounter > 0 && (c == '{' || c == '}' || c == ';'))
            {
                Buffer_AppendChar(&buffer, c);
                continue;
            }
        }

        if (c == '{')
        {
            if (curlyGroupCounter == 0 && pending == PENDING_NONE)
            {
                Buffer candidateText = {};
                const Slice trimmedBuffer = Slice_Trim(Buffer_Slice(&buffer));
                Buffer_Append(&candidateText, trimmedBuffer.Data, trimmedBuffer.Length);
                Buffer_Clear(&buffer);

                const Slice candidate = Buffer_Slice(&candidateText);

                if (Slice_StartsWithKeyword(candidate, "namespace"))
                {
                    Namespaces_Push(&namespaces, Slice_Trim(Slice{candidate.Data + 9, candidate.Length - 9}));
                    statementStart = i + 1;
                    Buffer_Free(&candidateText);
                    continue;
                }

                if (candidate.Length == 6 && strncmp(candidate.Data, "extern", 6) == 0)
                {
                    Namespaces_PushText(&namespaces, "extern \"C\" { ", " }", false);
                    statementStart = i + 1;
                    Buffer_Free(&candidateText);
                    continue;
                }

                const int assign = FindTopLevelAssign(candidate);
                Buffer_Clear(&pendingHead);

                if (assign >= 0)
                {
                    Buffer_Append(&pendingHead, candidate.Data, candidate.Length);
                    pending = PENDING_VARIABLE;
                }
                else if (IsClassLike(candidate))
                {
                    Buffer_Append(&pendingHead, candidate.Data, candidate.Length);
                    pending = PENDING_CLASS;
                    bodyStart = i + 1;
                }
                else if (candidate.Length > 0 && HasTopLevelParenthesis(candidate) && !IsMacroInvocation(candidate, macros))
                {
                    if (Slice_StartsWithKeyword(candidate, "extern"))
                    {
                        const Slice raw = Slice_Trim(Slice{scanContent.Data + statementStart, i > statementStart ? i - statementStart : 0});
                        const Slice afterExtern = raw.Length > 6 ? Slice_Trim(Slice{raw.Data + 6, raw.Length - 6}) : Slice{raw.Data, 0};
                        const bool linkage = strncmp(raw.Data, "extern", 6) == 0 && afterExtern.Length > 0 && afterExtern.Data[0] == '"';

                        if (linkage)
                        {
                            Namespaces_PushText(&namespaces, "extern \"C\" { ", " }", false);
                        }

                        AddFunction(Slice_Trim(Slice{candidate.Data + 6, candidate.Length - 6}), &namespaces, items);

                        if (linkage)
                        {
                            Namespaces_Pop(&namespaces);
                        }
                    }
                    else
                    {
                        AddFunction(candidate, &namespaces, items);
                    }
                }
                else if (candidate.Length > 0 && !HasTopLevelParenthesis(candidate) && !Slice_IsBareIdentifier(candidate))
                {
                    Buffer_Append(&pendingHead, candidate.Data, candidate.Length);
                    pending = PENDING_VARIABLE;
                }

                Buffer_Free(&candidateText);
            }

            curlyGroupCounter++;
        }
        else if (c == '}')
        {
            if (curlyGroupCounter == 0 && namespaces.Depth > 0)
            {
                Namespaces_Pop(&namespaces);
                Buffer_Clear(&buffer);
                pending = PENDING_NONE;
                statementStart = i + 1;
                continue;
            }

            curlyGroupCounter--;

            if (curlyGroupCounter == 0 && pending == PENDING_CLASS && pendingHead.Data && strchr(pendingHead.Data, '\x01'))
            {
                Buffer_AppendText(&buffer, "{}");
                continue;
            }

            if (curlyGroupCounter == 0)
            {
                Buffer_Clear(&buffer);

                if (pending == PENDING_CLASS)
                {
                    Buffer body = {};
                    Buffer_Append(&body, scanContent.Data + bodyStart, i - bodyStart);
                    Buffer_AppendChar(&pendingHead, '\x01');
                    Buffer_Append(&pendingHead, body.Data, body.Length);
                    Buffer_Free(&body);
                }
                else if (pending == PENDING_NONE)
                {
                    statementStart = i + 1;
                }
            }
        }
        else if (c == ';' && curlyGroupCounter == 0)
        {
            Buffer tail = {};
            const Slice trimmedBuffer = Slice_Trim(Buffer_Slice(&buffer));
            Buffer_Append(&tail, trimmedBuffer.Data, trimmedBuffer.Length);
            Buffer_Clear(&buffer);

            Buffer raw = {};
            RawText(scanContent, statementStart, i + 1, &raw);

            if (pending == PENDING_VARIABLE)
            {
                const Slice head = Buffer_Slice(&pendingHead);
                AddStatement(head, Buffer_Slice(&raw), &namespaces, items);
            }
            else if (pending == PENDING_CLASS)
            {
                char *separator = pendingHead.Data ? strchr(pendingHead.Data, '\x01') : NULL;
                Slice head = {pendingHead.Data, separator ? (size_t)(separator - pendingHead.Data) : pendingHead.Length};
                Slice body = separator ? Slice{separator + 1, strlen(separator + 1)} : Slice{"", 0};
                const Slice afterBody = Slice_Trim(Buffer_Slice(&tail));
                const bool isTypedef = Slice_StartsWithKeyword(head, "typedef");
                List names = {};
                Buffer tag = {};

                if (ClassTagName(head, &tag))
                {
                    List_Add(&names, tag.Data);
                }

                if (isTypedef && afterBody.Length > 0)
                {
                    List parts = {};
                    SplitTopLevelCommas(afterBody, &parts);
                    Buffer alias = {};

                    for (int p = 0; p < parts.Count; p++)
                    {
                        if (DeclaratorName(Slice{parts.Items[p], strlen(parts.Items[p])}, &alias))
                        {
                            List_Add(&names, alias.Data);
                        }
                    }

                    Buffer_Free(&alias);
                    List_Free(&parts);
                }

                if (Slice_StartsWithKeyword(SkipSpecifiers(head), "enum") && !ContainsWordSlice(head, "class") && !ContainsWordSlice(head, "struct"))
                {
                    EnumeratorNames(body, &names);
                }

                if (namespaces.PrivateDepth == 0 && !isTypedef && afterBody.Length > 0 && tag.Length > 0 && !Slice_StartsWithKeyword(head, "template") && !Slice_StartsWithKeyword(head, "static"))
                {
                    const Slice body = SkipSpecifiers(head);
                    const char *keyword = Slice_StartsWithKeyword(body, "struct") ? "struct " : Slice_StartsWithKeyword(body, "class") ? "class " : Slice_StartsWithKeyword(body, "union") ? "union " : "enum ";
                    Buffer variable = {};
                    Buffer declaration = {};
                    List variableNames = {};
                    Buffer_AppendText(&variable, keyword);
                    Buffer_AppendText(&variable, tag.Data);
                    Buffer_AppendChar(&variable, ' ');
                    Buffer_Append(&variable, afterBody.Data, afterBody.Length);

                    if (DeclarationNames(Buffer_Slice(&variable), &variableNames) && Classify(Buffer_Slice(&variable), &declaration))
                    {
                        Items_Add(items, 'P', declaration.Data, declaration.Data, &namespaces, &variableNames);
                    }

                    List_Free(&variableNames);
                    Buffer_Free(&declaration);
                    Buffer_Free(&variable);
                }

                if (namespaces.PrivateDepth == 0 && names.Count > 0 && (isTypedef || afterBody.Length == 0 || tag.Length > 0))
                {
                    const char *text = raw.Data ? raw.Data : "";
                    Buffer forward = {};
                    List covered = {};
                    List rest = {};
                    ClassForward(head, isTypedef ? afterBody : Slice{"", 0}, &forward, &covered);

                    for (int n = 0; n < names.Count; n++)
                    {
                        if (!List_Contains(&covered, names.Items[n]))
                        {
                            List_Add(&rest, names.Items[n]);
                        }
                    }

                    if (covered.Count > 0)
                    {
                        Items_Add(items, 'C', text, forward.Data, &namespaces, &covered);
                    }

                    if (rest.Count > 0)
                    {
                        Items_Add(items, 'F', text, text, &namespaces, &rest);
                    }

                    List_Free(&rest);
                    List_Free(&covered);
                    Buffer_Free(&forward);
                }

                Buffer_Free(&tag);
                List_Free(&names);
            }
            else if (!IsMacroInvocation(Buffer_Slice(&tail), macros))
            {
                AddStatement(Buffer_Slice(&tail), Buffer_Slice(&raw), &namespaces, items);
            }

            pending = PENDING_NONE;
            statementStart = i + 1;
            Buffer_Free(&raw);
            Buffer_Free(&tail);
        }
        else if (curlyGroupCounter == 0)
        {
            Buffer_AppendChar(&buffer, c);
        }
    }

    Buffer_Free(&buffer);
    Buffer_Free(&pendingHead);
    Namespaces_Free(&namespaces);
}

typedef struct
{
    List Names;
    List Kinds;
    List Lines;
    List OwnerLines;
    List Spaces;
    List Owners;
} SharedNames;

static SharedNames Shared = {};

static List ScannedPaths = {};
static List ScannedTexts = {};

static void Shared_Add(const char *name, char kind, const char *line, const char *ownerLine, const char *space, const char *owner)
{
    const char kindText[2] = {kind, '\0'};
    List_Add(&Shared.Names, name);
    List_Add(&Shared.Kinds, kindText);
    List_Add(&Shared.Lines, line);
    List_Add(&Shared.OwnerLines, ownerLine);
    List_Add(&Shared.Spaces, space);
    List_Add(&Shared.Owners, owner);
}

static bool ProcessSystemsFile(const char *sourcePath, const char *owner)
{
    Buffer content = {};

    if (!ReadSource(sourcePath, &content))
    {
        fprintf(stderr, "[Includes] Skipped '%s': could not read the file.\n", sourcePath);
        Buffer_Free(&content);
        return false;
    }

    Buffer scanContent = {};
    Macros macros = {};
    Items items = {};
    GetMacros(Buffer_Slice(&content), &macros);
    StripDirectives(Buffer_Slice(&content), &scanContent);
    ScanOwner = owner;
    ScanDeclarations(Buffer_Slice(&scanContent), &macros, &items);

    List referencedMacroNames = {};

    for (int m = 0; m < macros.Names.Count; m++)
    {
        for (int d = 0; d < items.Lines.Count; d++)
        {
            if (ContainsWholeWord(items.Lines.Items[d], macros.Names.Items[m]))
            {
                List_Add(&referencedMacroNames, macros.Names.Items[m]);
                break;
            }
        }
    }

    bool *needed = (bool *)calloc(macros.Names.Count + 1, sizeof(bool));
    ResolveMacroClosure(&macros, &referencedMacroNames, needed);

    for (int m = 0; m < macros.Names.Count; m++)
    {
        if (needed[m])
        {
            Shared_Add(macros.Names.Items[m], 'M', macros.Lines.Items[m], macros.Lines.Items[m], "", owner);
        }
    }

    for (int d = 0; d < items.Lines.Count; d++)
    {
        List names = {};
        Slice joined = {items.Names.Items[d], strlen(items.Names.Items[d])};
        SplitTopLevelCommas(joined, &names);

        if (items.Kinds.Items[d][0] == 'A' || joined.Length == 0)
        {
            Shared_Add("", 'A', items.Lines.Items[d], items.OwnerLines.Items[d], items.Spaces.Items[d], owner);
        }
        else
        {
            for (int n = 0; n < names.Count; n++)
            {
                Shared_Add(names.Items[n], items.Kinds.Items[d][0], items.Lines.Items[d], items.OwnerLines.Items[d], items.Spaces.Items[d], owner);
            }
        }

        List_Free(&names);
    }

    List_Add(&ScannedPaths, sourcePath);
    List_Add(&ScannedTexts, content.Data ? content.Data : "");

    free(needed);
    List_Free(&referencedMacroNames);
    Items_Free(&items);
    Macros_Free(&macros);
    Buffer_Free(&scanContent);
    Buffer_Free(&content);
    return true;
}

#pragma endregion

#pragma region Includes: Generation
/* Framework\Includes\Types.hpp (with the state signature hot reload checks) and Datas.hpp */

static bool ScanSystems(const char *systemsDirectory)
{
    if (!DirectoryExists(systemsDirectory))
    {
        return true;
    }

    List sources = {};
    bool succeeded = true;
    const size_t systemsPrefixLength = strlen(systemsDirectory) + 1;
    ListEntries(systemsDirectory, &sources, NULL);

    for (int i = 0; i < sources.Count; i++)
    {
        if (IsCodeFile(sources.Items[i]))
        {
            succeeded &= ProcessSystemsFile(sources.Items[i], sources.Items[i] + systemsPrefixLength);
        }
    }

    List_Free(&sources);
    return succeeded;
}

static void HashSource(const char *path, unsigned int *hash)
{
    Buffer content = {};

    if (ReadSource(path, &content))
    {
        for (size_t i = 0; i < content.Length; i++)
        {
            if (!IsWhitespace(content.Data[i]))
            {
                *hash = (*hash ^ (unsigned char)content.Data[i]) * 16777619u;
            }
        }
    }

    Buffer_Free(&content);
}

/* Every Types file and Utilities.hpp: Types.hpp includes them all, so State can use any */
static unsigned int StateSignature(const char *root)
{
    char path[MAX_PATH_LENGTH];
    List files = {};
    unsigned int hash = 2166136261u;
    snprintf(path, sizeof(path), "%s/Program/Code/Types", root);
    ListEntries(path, &files, NULL);

    if (files.Count > 1)
    {
        qsort(files.Items, files.Count, sizeof(char *), CompareInsensitiveItems);
    }

    for (int i = 0; i < files.Count; i++)
    {
        if (IsCodeFile(files.Items[i]))
        {
            HashSource(files.Items[i], &hash);
        }
    }

    snprintf(path, sizeof(path), "%s/Framework/Utilities.hpp", root);
    HashSource(path, &hash);
    List_Free(&files);
    return hash;
}

static bool NeedsInline(Slice statement)
{
    statement = Slice_Trim(statement);
    static const char *skipped[] = {"typedef", "using", "namespace", "template", "static_assert", "_Static_assert", "extern", "inline",
                                    "constexpr", "consteval", "constinit", "static", "friend"};

    for (size_t k = 0; k < sizeof(skipped) / sizeof(skipped[0]); k++)
    {
        if (Slice_StartsWithKeyword(statement, skipped[k]))
        {
            return false;
        }
    }

    if (Slice_StartsWithKeyword(statement, "struct") || Slice_StartsWithKeyword(statement, "class") || Slice_StartsWithKeyword(statement, "union") ||
        Slice_StartsWithKeyword(statement, "enum"))
    {
        const char *brace = (const char *)memchr(statement.Data, '{', statement.Length);
        const char *end = statement.Data + statement.Length;
        const int assign = FindTopLevelAssign(statement);

        if (assign >= 0 && (!brace || statement.Data + assign < brace))
        {
            return true;
        }

        if (!brace)
        {
            Slice rest = statement;
            const Slice afterKeyword = Slice_Trim(Slice{statement.Data + 4, statement.Length - 4});
            const bool scopedEnum = Slice_StartsWithKeyword(statement, "enum") && (Slice_StartsWithKeyword(afterKeyword, "class") || Slice_StartsWithKeyword(afterKeyword, "struct"));

            for (int word = 0; word < (scopedEnum ? 3 : 2); word++)
            {
                rest = Slice_Trim(rest);

                while (rest.Length > 0 && IsIdentChar(rest.Data[0]))
                {
                    rest.Data++;
                    rest.Length--;
                }
            }

            rest = Slice_Trim(rest);
            return rest.Length > 0 && rest.Data[0] != ':';
        }

        int depth = 0;
        const char *cursor = brace;

        for (; cursor < end; cursor++)
        {
            depth += *cursor == '{' ? 1 : *cursor == '}' ? -1 : 0;

            if (depth == 0)
            {
                break;
            }
        }

        for (cursor++; cursor < end; cursor++)
        {
            if (!IsWhitespace(*cursor))
            {
                return true;
            }
        }

        return false;
    }

    return statement.Length > 0 && IsIdentStartChar(statement.Data[0]);
}

static void FindInlinePoints(Slice scanContent, List *points)
{
    int depth = 0;
    int namespaceDepth = 0;
    int parenDepth = 0;
    bool inString = false;
    bool inChar = false;
    bool functionBody = false;
    size_t start = (size_t)-1;

    for (size_t i = 0; i <= scanContent.Length; i++)
    {
        const char c = i < scanContent.Length ? scanContent.Data[i] : ';';

        if (c == '\n')
        {
            inString = false;
            inChar = false;
            continue;
        }

        if (inString || inChar)
        {
            if (c == '\\')
            {
                i++;
            }
            else if ((inString && c == '"') || (inChar && c == '\''))
            {
                inString = inChar = false;
            }

            continue;
        }

        if (depth == 0 && start == (size_t)-1)
        {
            if (IsWhitespace(c) || c == ';' || (c == '}' && namespaceDepth == 0))
            {
                continue;
            }

            if (c == '}')
            {
                namespaceDepth--;
                continue;
            }

            start = i;
        }

        if (c == '"')
        {
            const size_t rawEnd = i < scanContent.Length ? RawStringEnd(scanContent, i) : 0;

            if (rawEnd > 0)
            {
                i = rawEnd - 1;
                continue;
            }

            inString = true;
        }
        else if (c == '\'' && !IsDigitSeparator(scanContent, i))
        {
            inChar = true;
        }
        else if (depth == 0 && c == '(')
        {
            parenDepth++;
        }
        else if (depth == 0 && c == ')')
        {
            parenDepth -= parenDepth > 0 ? 1 : 0;
        }
        else if (depth == 0 && parenDepth > 0 && i < scanContent.Length)
        {
        }
        else if (c == '{')
        {
            if (depth == 0)
            {
                const Slice head = Slice_Trim(Slice{scanContent.Data + start, i - start});

                if (Slice_StartsWithKeyword(head, "namespace") || (head.Length == 6 && strncmp(head.Data, "extern", 6) == 0) || Slice_StartsWithKeyword(head, "extern"))
                {
                    if (Slice_StartsWithKeyword(head, "namespace") || head.Length <= 10)
                    {
                        namespaceDepth++;
                        start = (size_t)-1;
                        continue;
                    }
                }

                functionBody = FindTopLevelAssign(head) < 0 && HasTopLevelParenthesis(head) && !IsClassLike(head);
            }

            depth++;
        }
        else if (c == '}')
        {
            depth--;

            if (depth == 0 && functionBody)
            {
                if (NeedsInline(Slice{scanContent.Data + start, i - start}))
                {
                    char offset[32];
                    snprintf(offset, sizeof(offset), "%zu", start);
                    List_Add(points, offset);
                }

                functionBody = false;
                start = (size_t)-1;
            }
        }
        else if (c == ';' && depth == 0)
        {
            if (i < scanContent.Length && NeedsInline(Slice{scanContent.Data + start, i - start}))
            {
                char offset[32];
                snprintf(offset, sizeof(offset), "%zu", start);
                List_Add(points, offset);
            }

            start = (size_t)-1;
        }
    }
}

static bool WriteInlineData(const char *sourcePath, const char *outputPath)
{
    Buffer content = {};

    if (!ReadSource(sourcePath, &content))
    {
        fprintf(stderr, "[Includes] Could not read '%s'.\n", sourcePath);
        Buffer_Free(&content);
        return false;
    }

    Buffer scanContent = {};
    List points = {};
    StripDirectives(Buffer_Slice(&content), &scanContent);
    FindInlinePoints(Buffer_Slice(&scanContent), &points);

    Buffer output = {};
    Buffer_AppendText(&output, "#line 1 \"");
    Buffer_AppendText(&output, sourcePath);
    Buffer_AppendText(&output, "\"\r\n");

    size_t scanLine = 0;
    size_t scanColumn = 0;
    size_t scanOffset = 0;
    size_t contentLine = 0;
    size_t contentColumn = 0;
    int next = 0;

    for (size_t i = 0; i < content.Length; i++)
    {
        while (next < points.Count)
        {
            const size_t target = (size_t)strtoull(points.Items[next], NULL, 10);

            while (scanOffset < target)
            {
                if (scanContent.Data[scanOffset] == '\n')
                {
                    scanLine++;
                    scanColumn = 0;
                }
                else
                {
                    scanColumn++;
                }

                scanOffset++;
            }

            if (scanLine == contentLine && scanColumn == contentColumn)
            {
                Buffer_AppendText(&output, "inline ");
                next++;
                continue;
            }

            break;
        }

        const char c = content.Data[i];

        if (c == '\r')
        {
            continue;
        }

        if (c == '\n')
        {
            contentLine++;
            contentColumn = 0;
            Buffer_AppendText(&output, "\r\n");
            continue;
        }

        contentColumn++;
        Buffer_AppendChar(&output, c);
    }

    const bool written = WriteIfChanged(outputPath, &output);

    List_Free(&points);
    Buffer_Free(&output);
    Buffer_Free(&scanContent);
    Buffer_Free(&content);
    return written;
}

static bool GenerateDatas(const char *datasDirectory, const char *copiesDirectory, Buffer *aggregate)
{
    List sources = {};
    List generated = {};
    bool succeeded = true;

    if (DirectoryExists(datasDirectory))
    {
        ListEntries(datasDirectory, &sources, NULL);
    }

    if (sources.Count > 1)
    {
        qsort(sources.Items, sources.Count, sizeof(char *), CompareInsensitiveItems);
    }

    const size_t prefixLength = strlen(datasDirectory) + 1;

    for (int i = 0; i < sources.Count; i++)
    {
        if (!IsCodeFile(sources.Items[i]))
        {
            continue;
        }

        char outputPath[MAX_PATH_LENGTH];
        snprintf(outputPath, sizeof(outputPath), "%s/%s", copiesDirectory, sources.Items[i] + prefixLength);

        for (char *cursor = outputPath + strlen(copiesDirectory) + 1; *cursor; cursor++)
        {
            if (*cursor == '/')
            {
                *cursor = '\0';
                EnsureDirectory(outputPath);
                *cursor = '/';
            }
        }

        EnsureDirectory(copiesDirectory);
        succeeded &= WriteInlineData(sources.Items[i], outputPath);
        List_Add(&generated, outputPath);

        Buffer_AppendText(aggregate, "#include <Framework/Includes/Datas/");
        Buffer_AppendText(aggregate, sources.Items[i] + prefixLength);
        Buffer_AppendText(aggregate, ">\r\n");
    }

    DeleteStale(copiesDirectory, &generated);
    Buffer_AppendText(aggregate, "\r\n");
    List_Free(&generated);
    List_Free(&sources);
    return succeeded;
}

static void EmitSection(Buffer *target, const char *directory, const char *baseDirectory, const char *codePrefix, const char *lastFile)
{
    if (!DirectoryExists(directory))
    {
        fprintf(stderr, "[Includes] WARNING: \"%s\" does not exist, skipping.\n", directory);
        return;
    }

    List files = {};
    List headers = {};
    ListEntries(directory, &files, NULL);

    for (int i = 0; i < files.Count; i++)
    {
        if (IsCodeFile(files.Items[i]) && !(lastFile && _stricmp(FileName(files.Items[i]), lastFile) == 0))
        {
            List_Add(&headers, files.Items[i]);
        }
    }

    if (headers.Count > 1)
    {
        qsort(headers.Items, headers.Count, sizeof(char *), CompareInsensitiveItems);
    }

    const size_t baseLength = strlen(baseDirectory) + 1;
    bool found = false;

    for (int i = 0; i < headers.Count; i++)
    {
        Buffer_AppendText(target, "#include <");
        Buffer_AppendText(target, codePrefix);
        Buffer_AppendChar(target, '/');
        Buffer_AppendText(target, headers.Items[i] + baseLength);
        Buffer_AppendText(target, ">\r\n");
        found = true;
    }

    if (lastFile)
    {
        char lastPath[MAX_PATH_LENGTH];
        snprintf(lastPath, sizeof(lastPath), "%s/%s", directory, lastFile);

        if (FileExists(lastPath))
        {
            Buffer_AppendText(target, "#include <");
            Buffer_AppendText(target, codePrefix);
            Buffer_AppendChar(target, '/');
            Buffer_AppendText(target, lastPath + baseLength);
            Buffer_AppendText(target, ">\r\n");
            found = true;
        }
    }

    if (!found)
    {
        printf("[Includes] NOTE: no headers found under \"%s\"\n", directory);
    }

    Buffer_AppendText(target, "\r\n");

    List_Free(&files);
    List_Free(&headers);
}

#pragma endregion

#pragma region Includes: Uses
/* Per-unit Uses headers, ForwardDeclarations.hpp, the project's files and platforms, and Tools Includes */

static int *SharedOrder = NULL;

static int CompareSharedIndexes(const void *a, const void *b)
{
    return strcmp(Shared.Names.Items[*(const int *)a], Shared.Names.Items[*(const int *)b]);
}

static int CompareItems(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void SortShared(void)
{
    free(SharedOrder);
    SharedOrder = (int *)malloc(sizeof(int) * (size_t)(Shared.Names.Count + 1));

    for (int i = 0; i < Shared.Names.Count; i++)
    {
        SharedOrder[i] = i;
    }

    qsort(SharedOrder, (size_t)Shared.Names.Count, sizeof(int), CompareSharedIndexes);
}

static int FindShared(const char *name)
{
    int low = 0;
    int high = Shared.Names.Count;

    while (low < high)
    {
        const int middle = (low + high) / 2;

        if (strcmp(Shared.Names.Items[SharedOrder[middle]], name) < 0)
        {
            low = middle + 1;
        }
        else
        {
            high = middle;
        }
    }

    return (low < Shared.Names.Count && strcmp(Shared.Names.Items[SharedOrder[low]], name) == 0) ? low : -1;
}

static void CollectIdentifiers(Slice text, List *identifiers)
{
    size_t i = 0;

    while (i < text.Length)
    {
        if (!IsIdentChar(text.Data[i]))
        {
            i++;
            continue;
        }

        const size_t start = i;

        while (i < text.Length && IsIdentChar(text.Data[i]))
        {
            i++;
        }

        if (IsIdentStartChar(text.Data[start]))
        {
            Buffer word = {};
            Buffer_Append(&word, text.Data + start, i - start);
            List_Add(identifiers, word.Data);
            Buffer_Free(&word);
        }
    }
}

static char *Excluded = NULL;

static const char *SharedText(int index)
{
    return Shared.OwnerLines.Items[index];
}

static bool IsConstVariable(int index)
{
    const char *start = strstr(SharedText(index), "extern ");
    const char *name = start;
    const size_t length = strlen(Shared.Names.Items[index]);

    while (name && (name = strstr(name + 1, Shared.Names.Items[index])) && (IsIdentChar(name[-1]) || IsIdentChar(name[length])))
    {
    }

    if (!name)
    {
        return false;
    }

    Buffer prefix = {};
    Buffer_AppendChar(&prefix, ' ');

    for (const char *cursor = start; cursor < name; cursor++)
    {
        if (*cursor == '*' || *cursor == '&')
        {
            Buffer_Clear(&prefix);
        }
        else
        {
            Buffer_AppendChar(&prefix, *cursor);
        }
    }

    const char *after = name + length;

    while (IsWhitespace(*after))
    {
        after++;
    }

    const bool isConst = *after != '(' && ContainsWholeWord(prefix.Data, "const");
    Buffer_Free(&prefix);
    return isConst;
}

static bool UsesExcluded(int index, bool arraysNeedClasses)
{
    const char *line = SharedText(index);
    List words = {};
    CollectIdentifiers(Slice{line, strlen(line)}, &words);
    const bool isArray = arraysNeedClasses && strchr(line, '[') != NULL;
    bool uses = false;

    for (int w = 0; w < words.Count && !uses; w++)
    {
        if (strcmp(words.Items[w], Shared.Names.Items[index]) == 0)
        {
            continue;
        }

        bool found = false;
        bool available = false;

        for (int position = FindShared(words.Items[w]); position >= 0 && position < Shared.Names.Count && strcmp(Shared.Names.Items[SharedOrder[position]], words.Items[w]) == 0; position++)
        {
            const int other = SharedOrder[position];
            found = true;

            if (!Excluded[other] && !(isArray && (Shared.Kinds.Items[other][0] == 'C' || (Shared.Kinds.Items[other][0] == 'P' && IsConstVariable(other)))))
            {
                available = true;
            }
        }

        uses = found && !available;
    }

    List_Free(&words);
    return uses;
}

static void ComputeExcluded(void)
{
    free(Excluded);
    Excluded = (char *)calloc((size_t)Shared.Names.Count + 1, 1);

    for (int i = 0; i < Shared.Names.Count; i++)
    {
        Excluded[i] = strchr("FI", Shared.Kinds.Items[i][0]) != NULL;
    }

    for (bool changed = true; changed;)
    {
        changed = false;

        for (int i = 0; i < Shared.Names.Count; i++)
        {
            if (!Excluded[i] && Shared.Kinds.Items[i][0] != 'M' && Shared.Kinds.Items[i][0] != 'C' && UsesExcluded(i, true))
            {
                Excluded[i] = 1;
                changed = true;
            }
        }
    }
}

static bool DefaultedParameters(const char *line, Buffer *parameters, Buffer *defaults)
{
    const char *close = strrchr(line, ')');
    const int open = close ? OpeningBracket(Slice{line, (size_t)(close - line) + 1}, '(', ')') : -1;
    List split = {};
    bool complete = open >= 0;

    if (complete)
    {
        SplitTopLevelCommas(Slice{line + open + 1, (size_t)(close - line) - (size_t)open - 1}, &split);
    }

    for (int p = 0; p < split.Count; p++)
    {
        const Slice parameter = {split.Items[p], strlen(split.Items[p])};
        const int assign = FindTopLevelAssign(parameter);

        if (assign >= 0)
        {
            const Slice value = Slice_Trim(Slice{parameter.Data + assign + 1, parameter.Length - (size_t)assign - 1});
            complete = complete && value.Length > 0;
            Buffer_Append(parameters, parameter.Data, parameter.Length);
            Buffer_Append(defaults, value.Data, value.Length);
            Buffer_AppendChar(parameters, ' ');
            Buffer_AppendChar(defaults, ' ');
        }
    }

    List_Free(&split);
    return complete && parameters->Length > 0;
}

static char *Shareable = NULL;

static void ComputeShareable(void)
{
    free(Shareable);
    Shareable = (char *)calloc((size_t)Shared.Names.Count + 1, 1);

    for (int i = 0; i < Shared.Names.Count; i++)
    {
        if (Excluded[i] || strcmp(Shared.Lines.Items[i], Shared.OwnerLines.Items[i]) == 0)
        {
            continue;
        }

        Buffer parameters = {};
        Buffer defaults = {};
        List words = {};
        List defaultWords = {};
        bool shareable = DefaultedParameters(Shared.Lines.Items[i], &parameters, &defaults);
        CollectIdentifiers(Buffer_Slice(&parameters), &words);
        CollectIdentifiers(Buffer_Slice(&defaults), &defaultWords);

        for (int w = 0; w < defaultWords.Count && shareable; w++)
        {
            char key[MAX_PATH_LENGTH + 256];
            snprintf(key, sizeof(key), "%s|%s", Shared.Owners.Items[i], defaultWords.Items[w]);
            shareable = !List_Contains(&PrivateNames, key);
        }

        for (int w = 0; w < words.Count && shareable; w++)
        {
            bool found = false;
            bool available = false;

            for (int position = FindShared(words.Items[w]); position >= 0 && position < Shared.Names.Count && strcmp(Shared.Names.Items[SharedOrder[position]], words.Items[w]) == 0; position++)
            {
                const int other = SharedOrder[position];
                found = true;
                available |= !Excluded[other] && Shared.Kinds.Items[other][0] != 'C';
            }

            shareable = !found || available;
        }

        Shareable[i] = shareable;
        List_Free(&words);
        List_Free(&defaultWords);
        Buffer_Free(&parameters);
        Buffer_Free(&defaults);
    }
}

static const char *EmitOwner = NULL;
static bool EmitDefaults = false;

static const char *EmittedText(int index)
{
    const bool own = EmitOwner && strcmp(Shared.Owners.Items[index], EmitOwner) == 0;
    return EmitDefaults && !own && Shareable[index] ? Shared.Lines.Items[index] : Shared.OwnerLines.Items[index];
}

static void EmitShared(int index, char *state, List *emittedLines, Buffer *output)
{
    if (state[index] != 0)
    {
        return;
    }

    state[index] = 1;

    if (Excluded[index])
    {
        state[index] = 2;
        return;
    }

    const char *line = EmittedText(index);
    List words = {};
    CollectIdentifiers(Slice{line, strlen(line)}, &words);

    for (int w = 0; w < words.Count; w++)
    {
        if (strcmp(words.Items[w], Shared.Names.Items[index]) == 0)
        {
            continue;
        }

        for (int position = FindShared(words.Items[w]); position >= 0 && position < Shared.Names.Count && strcmp(Shared.Names.Items[SharedOrder[position]], words.Items[w]) == 0; position++)
        {
            EmitShared(SharedOrder[position], state, emittedLines, output);
        }
    }

    List_Free(&words);
    state[index] = 2;

    if (!List_Contains(emittedLines, line))
    {
        List_Add(emittedLines, line);
        AppendSource(output, line);
    }
}

static int CompareEmitOrder(const void *a, const void *b)
{
    const int x = *(const int *)a;
    const int y = *(const int *)b;
    const int kind = (Shared.Kinds.Items[x][0] != 'M') - (Shared.Kinds.Items[y][0] != 'M');
    const int space = strcmp(Shared.Spaces.Items[x], Shared.Spaces.Items[y]);
    const int name = strcmp(Shared.Names.Items[x], Shared.Names.Items[y]);
    return kind ? kind : space ? space : name ? name : x - y;
}

static void EmitSorted(int *indexes, int count, char *state, List *emittedLines, Buffer *output)
{
    qsort(indexes, (size_t)count, sizeof(int), CompareEmitOrder);

    for (int i = 0; i < count; i++)
    {
        EmitShared(indexes[i], state, emittedLines, output);
    }
}

static void CollectReferences(Slice text, List *references)
{
    size_t i = 0;

    while (i < text.Length)
    {
        if (!IsIdentStartChar(text.Data[i]) || (i > 0 && IsIdentChar(text.Data[i - 1])))
        {
            i++;
            continue;
        }

        const size_t start = i;

        while (i < text.Length && IsIdentChar(text.Data[i]))
        {
            i++;
        }

        size_t before = start;

        while (before > 0 && IsWhitespace(text.Data[before - 1]))
        {
            before--;
        }

        Buffer reference = {};

        if (before > 0 && (text.Data[before - 1] == '.' || (before > 1 && text.Data[before - 1] == '>' && text.Data[before - 2] == '-')))
        {
            continue;
        }

        if (before > 1 && text.Data[before - 1] == ':' && text.Data[before - 2] == ':')
        {
            size_t qualifierEnd = before - 2;

            while (qualifierEnd > 0 && IsWhitespace(text.Data[qualifierEnd - 1]))
            {
                qualifierEnd--;
            }

            size_t qualifierStart = qualifierEnd;

            while (qualifierStart > 0 && IsIdentChar(text.Data[qualifierStart - 1]))
            {
                qualifierStart--;
            }

            Buffer_Append(&reference, text.Data + qualifierStart, qualifierEnd - qualifierStart);
            Buffer_AppendText(&reference, "::");
        }

        Buffer_Append(&reference, text.Data + start, i - start);

        if (!List_Contains(references, reference.Data))
        {
            List_Add(references, reference.Data);
        }

        Buffer_Free(&reference);
    }
}

static bool RefersTo(const List *references, int index)
{
    const char *name = Shared.Names.Items[index];

    if (List_Contains(references, name))
    {
        return true;
    }

    const char *space = Shared.Spaces.Items[index];
    const char *last = NULL;

    for (const char *found = strstr(space, "namespace "); found; found = strstr(found + 1, "namespace "))
    {
        last = found + 10;
    }

    if (!last)
    {
        return false;
    }

    Buffer qualified = {};
    size_t length = 0;

    while (IsIdentChar(last[length]))
    {
        length++;
    }

    Buffer_Append(&qualified, last, length);
    Buffer_AppendText(&qualified, "::");
    Buffer_AppendText(&qualified, name);
    const bool found = List_Contains(references, qualified.Data);
    Buffer_Free(&qualified);
    return found;
}

static void AddHints(const char *unitOwner, const List *identifiers, const List *references, Buffer *output)
{
    for (int i = 0; i < identifiers->Count; i++)
    {
        if (i > 0 && strcmp(identifiers->Items[i], identifiers->Items[i - 1]) == 0)
        {
            continue;
        }

        int hidden = -1;
        int className = -1;
        int defaults = -1;
        bool visible = false;

        for (int position = FindShared(identifiers->Items[i]); position >= 0 && position < Shared.Names.Count && strcmp(Shared.Names.Items[SharedOrder[position]], identifiers->Items[i]) == 0; position++)
        {
            const int index = SharedOrder[position];

            if (!RefersTo(references, index))
            {
                continue;
            }

            if (unitOwner && strcmp(Shared.Owners.Items[index], unitOwner) == 0)
            {
                visible = true;
            }
            else if (Excluded[index])
            {
                hidden = index;
            }
            else if (Shared.Kinds.Items[index][0] == 'C')
            {
                className = index;
            }
            else
            {
                visible = true;

                if (Shared.Kinds.Items[index][0] == 'P' && !Shareable[index] && strcmp(Shared.Lines.Items[index], Shared.OwnerLines.Items[index]) != 0)
                {
                    defaults = index;
                }
            }
        }

        char line[1024];

        if (defaults >= 0)
        {
            snprintf(line, sizeof(line), "// HINT: The default arguments of %s (Systems/%s) use something only that script sees, so other scripts must pass every argument. Move what they use to a Types file.\r\n", identifiers->Items[i], Shared.Owners.Items[defaults]);
            Buffer_AppendText(output, line);
        }

        if (hidden >= 0 && !visible && className < 0)
        {
            const char kind = Shared.Kinds.Items[hidden][0];
            const char *what = kind == 'I' ? "is an inline, template or constexpr function" : (kind == 'P' || kind == 'T') ? "is declared with something only that script sees (a constexpr size, a whole class)" : "is a template, constant, enum or class body";
            snprintf(line, sizeof(line), "// HINT: %s is in Systems/%s, but it %s, so other scripts can't see it. Move it to a Types file.\r\n", identifiers->Items[i], Shared.Owners.Items[hidden], what);
            Buffer_AppendText(output, line);
        }
        else if (className >= 0 && !visible)
        {
            snprintf(line, sizeof(line), "// HINT: Other scripts only see the name of %s (Systems/%s). To use what's inside it, move it to a Types file.\r\n", identifiers->Items[i], Shared.Owners.Items[className]);
            Buffer_AppendText(output, line);
        }
    }
}

static bool WriteUsesHeader(const char *usesPath, const char *source, const char *unitOwner)
{
    List identifiers = {};
    List references = {};
    Buffer content = {};
    int scanned = -1;

    for (int k = 0; k < ScannedPaths.Count && scanned < 0; k++)
    {
        scanned = strcmp(ScannedPaths.Items[k], source) == 0 ? k : -1;
    }

    if (scanned >= 0)
    {
        Buffer_AppendText(&content, ScannedTexts.Items[scanned]);
    }
    else if (!ReadSource(source, &content))
    {
        fprintf(stderr, "[Includes] Could not read '%s'.\n", source);
        Buffer_Free(&content);
        return false;
    }

    CollectIdentifiers(Buffer_Slice(&content), &identifiers);
    CollectReferences(Buffer_Slice(&content), &references);
    Buffer_Free(&content);

    if (identifiers.Count > 1)
    {
        qsort(identifiers.Items, (size_t)identifiers.Count, sizeof(char *), CompareItems);
    }

    Buffer output = {};
    AppendLine(&output, "#pragma once");
    AppendLine(&output, "");
    AppendLine(&output, "#define FRAMEWORK_UNIT_DECLARATIONS");
    AppendLine(&output, "#include <Framework/Framework.hpp>");
    AppendLine(&output, "");

    char *state = (char *)calloc((size_t)Shared.Names.Count + 1, 1);
    int *order = (int *)malloc(sizeof(int) * (size_t)(Shared.Names.Count + 1));
    int count = 0;
    List emittedLines = {};
    EmitOwner = unitOwner;
    EmitDefaults = true;

    for (int i = 0; i < identifiers.Count; i++)
    {
        char key[MAX_PATH_LENGTH + 256];
        snprintf(key, sizeof(key), "%s|%s", unitOwner ? unitOwner : "", identifiers.Items[i]);

        /* Its own static or unnamed-namespace name, not another script's */
        if ((i > 0 && strcmp(identifiers.Items[i], identifiers.Items[i - 1]) == 0) || List_Contains(&PrivateNames, key))
        {
            continue;
        }

        for (int position = FindShared(identifiers.Items[i]); position >= 0 && position < Shared.Names.Count && strcmp(Shared.Names.Items[SharedOrder[position]], identifiers.Items[i]) == 0; position++)
        {
            order[count++] = SharedOrder[position];
        }
    }

    for (int i = 0; i < Shared.Names.Count; i++)
    {
        if (Shared.Kinds.Items[i][0] == 'A')
        {
            order[count++] = i;
        }
    }

    EmitSorted(order, count, state, &emittedLines, &output);
    free(order);

    AppendLine(&output, "");
    AddHints(unitOwner, &identifiers, &references, &output);
    const bool written = WriteIfChanged(usesPath, &output);

    if (!written)
    {
        fprintf(stderr, "[Includes] Could not write '%s'.\n", usesPath);
    }

    free(state);
    List_Free(&emittedLines);
    List_Free(&identifiers);
    List_Free(&references);
    Buffer_Free(&output);
    return written;
}

static bool WriteForwardDeclarations(const char *path)
{
    Buffer output = {};
    AppendLine(&output, "#pragma once");
    AppendLine(&output, "");
    AppendLine(&output, "#include <Framework/Includes/Types.hpp>");
    AppendLine(&output, "");

    char *state = (char *)calloc((size_t)Shared.Names.Count + 1, 1);
    int *order = (int *)malloc(sizeof(int) * (size_t)(Shared.Names.Count + 1));
    List emittedLines = {};
    EmitOwner = NULL;
    EmitDefaults = false;

    for (int i = 0; i < Shared.Names.Count; i++)
    {
        order[i] = i;
    }

    EmitSorted(order, Shared.Names.Count, state, &emittedLines, &output);
    free(order);

    const bool written = WriteIfChanged(path, &output);

    if (!written)
    {
        fprintf(stderr, "[Includes] Could not write '%s'.\n", path);
    }

    free(state);
    List_Free(&emittedLines);
    Buffer_Free(&output);
    return written;
}

static bool GenerateUsesHeaders(const char *programDirectory, const char *systemsDirectory, const char *usesDirectory)
{
    SortShared();
    ComputeExcluded();
    ComputeShareable();

    List generated = {};
    bool succeeded = true;
    char usesPath[MAX_PATH_LENGTH];

    if (DirectoryExists(systemsDirectory))
    {
        List files = {};
        ListEntries(systemsDirectory, &files, NULL);
        const size_t prefixLength = strlen(systemsDirectory) + 1;

        for (int i = 0; i < files.Count; i++)
        {
            if (!IsCodeFile(files.Items[i]))
            {
                continue;
            }

            snprintf(usesPath, sizeof(usesPath), "%s/Systems/%s", usesDirectory, files.Items[i] + prefixLength);
            succeeded &= WriteUsesHeader(usesPath, files.Items[i], files.Items[i] + prefixLength);
            List_Add(&generated, usesPath);
        }

        List_Free(&files);
    }

    char programSource[MAX_PATH_LENGTH];
    snprintf(programSource, sizeof(programSource), "%s/Program.cpp", programDirectory);

    if (FileExists(programSource))
    {
        snprintf(usesPath, sizeof(usesPath), "%s/Program.hpp", usesDirectory);
        succeeded &= WriteUsesHeader(usesPath, programSource, NULL);
        List_Add(&generated, usesPath);
    }

    DeleteStale(usesDirectory, &generated);
    List_Free(&generated);
    return succeeded;
}

#pragma endregion

static bool IsPlainName(const char *path)
{
    for (const unsigned char *cursor = (const unsigned char *)path; *cursor; cursor++)
    {
        if (*cursor >= 0x80 || *cursor == '?')
        {
            return false;
        }
    }

    return true;
}

static bool IsProjectFile(const char *name)
{
    return !strchr(name, '~') && !EndsWithExtension(name, ".tmp") && _stricmp(name, "Thumbs.db") != 0 && _stricmp(name, "desktop.ini") != 0 && _stricmp(name, ".DS_Store") != 0;
}

static bool UnitUsesHeader(const char *path, char *uses, size_t size)
{
    const bool code = EndsWithExtension(path, ".cpp") || EndsWithExtension(path, ".hpp") || EndsWithExtension(path, ".h");

    if (code && _strnicmp(path, "Program\\Code\\Systems\\", 21) == 0)
    {
        snprintf(uses, size, "Framework\\Includes\\Uses\\Systems\\%s", path + 21);
        return true;
    }

    if (_stricmp(path, "Program\\Code\\Program.cpp") == 0)
    {
        snprintf(uses, size, "Framework\\Includes\\Uses\\Program.hpp");
        return true;
    }

    return false;
}

static const char *ProjectItemType(const char *path)
{
    char uses[MAX_PATH_LENGTH];

    if (UnitUsesHeader(path, uses, sizeof(uses)) || EndsWithExtension(path, ".cpp") || EndsWithExtension(path, ".c"))
    {
        return "ClCompile";
    }

    return EndsWithExtension(path, ".hpp") || EndsWithExtension(path, ".h") ? "ClInclude" : "None";
}

static void RemoveProgramItems(const char *text, Buffer *output, List *items, List *filters, const List *dropped)
{
    Buffer kept = {};
    const char *line = text;
    bool skipping = false;
    int unit = -1;

    while (*line)
    {
        const char *next = strchr(line, '\n');
        next = next ? next + 1 : line + strlen(line);
        const char *trimmed = line;

        while (*trimmed == ' ' || *trimmed == '\t')
        {
            trimmed++;
        }

        const char *include = strstr(trimmed, " Include=\"");
        const char *includeEnd = include ? strchr(include + 10, '"') : NULL;
        char value[MAX_PATH_LENGTH] = "";

        if (includeEnd && includeEnd < next)
        {
            snprintf(value, sizeof(value), "%.*s", (int)(includeEnd - include - 10), include + 10);
        }

        const bool program = _strnicmp(value, "Program", 7) == 0 && (value[7] == '\\' || value[7] == '\0');
        const bool drop = !program && List_ContainsInsensitive(dropped, value);
        include = program || drop ? include : NULL;

        if (skipping)
        {
            if (unit >= 0 && strncmp(trimmed, "<ForcedIncludeFiles>", 20) == 0)
            {
                char marked[MAX_PATH_LENGTH + 8];
                snprintf(marked, sizeof(marked), "%s|unit", items->Items[unit]);
                free(items->Items[unit]);
                items->Items[unit] = _strdup(marked);
            }

            skipping = strncmp(trimmed, "</", 2) != 0;
        }
        else if (trimmed[0] == '<' && trimmed[1] != '/' && include && include < next)
        {
            const char *start = include + 10;
            const char *end = strchr(start, '"');
            char path[MAX_PATH_LENGTH];
            snprintf(path, sizeof(path), "%.*s", (int)(end - start), start);
            if (!drop)
            {
                List_Add(strncmp(trimmed, "<Filter ", 8) == 0 ? filters : items, path);
                unit = strncmp(trimmed, "<Filter ", 8) == 0 ? -1 : items->Count - 1;
            }
            const char *close = strstr(trimmed, "/>");
            skipping = !close || close > next;
        }
        else
        {
            Buffer_Append(&kept, line, (size_t)(next - line));
        }

        line = next;
    }

    line = kept.Data ? kept.Data : "";

    while (*line)
    {
        const char *next = strchr(line, '\n');
        next = next ? next + 1 : line + strlen(line);
        const char *after = next;

        while (*after == ' ' || *after == '\t')
        {
            after++;
        }

        const char *trimmed = line;

        while (*trimmed == ' ' || *trimmed == '\t')
        {
            trimmed++;
        }

        if (strncmp(trimmed, "<ItemGroup>", 11) == 0 && strncmp(after, "</ItemGroup>", 12) == 0)
        {
            const char *skip = strchr(after, '\n');
            line = skip ? skip + 1 : after + strlen(after);
            continue;
        }

        Buffer_Append(output, line, (size_t)(next - line));
        line = next;
    }

    Buffer_Free(&kept);
}

static bool SameEntries(const List *a, const List *b)
{
    if (a->Count != b->Count)
    {
        return false;
    }

    for (int i = 0; i < a->Count; i++)
    {
        if (!List_ContainsInsensitive(b, a->Items[i]))
        {
            return false;
        }
    }

    return true;
}

static void InsertBeforeProjectEnd(Buffer *text, const Buffer *block)
{
    char *end = text->Data ? strstr(text->Data, "</Project>") : NULL;

    if (!end)
    {
        return;
    }

    Buffer updated = {};
    Buffer_Append(&updated, text->Data, (size_t)(end - text->Data));
    Buffer_Append(&updated, block->Data, block->Length);
    Buffer_AppendText(&updated, end);
    Buffer_Free(text);
    *text = updated;
}

static List MovedStrays = {};

static void MoveFilteredStrays(const char *root, const char *filtersPath, List *moved)
{
    Buffer text = {};

    if (!FileExists(filtersPath) || !ReadWholeFile(filtersPath, &text) || !text.Data)
    {
        return;
    }

    for (const char *cursor = strstr(text.Data, " Include=\""); cursor; cursor = strstr(cursor + 1, " Include=\""))
    {
        const char *start = cursor + 10;
        const char *end = strchr(start, '"');
        const char *close = end ? strchr(end, '>') : NULL;

        if (!end || !close || close[-1] == '/' || _strnicmp(start, "Program\\", 8) == 0 || strncmp(cursor - 7, "<Filter", 7) == 0)
        {
            continue;
        }

        const char *filter = strstr(close, "<Filter>");
        const char *next = strstr(close, " Include=\"");
        const char *filterEnd = filter ? strstr(filter, "</Filter>") : NULL;

        if (!filterEnd || (next && next < filter) || _strnicmp(filter + 8, "Program\\", 8) != 0)
        {
            continue;
        }

        char include[MAX_PATH_LENGTH];
        char folder[MAX_PATH_LENGTH];
        snprintf(include, sizeof(include), "%.*s", (int)(end - start), start);
        snprintf(folder, sizeof(folder), "%.*s", (int)(filterEnd - filter - 8), filter + 8);

        if (strstr(include, "..") || strchr(include, ':'))
        {
            continue;
        }

        char from[MAX_PATH_LENGTH];
        char to[MAX_PATH_LENGTH];
        const char *name = strrchr(include, '\\') ? strrchr(include, '\\') + 1 : include;
        snprintf(from, sizeof(from), "%s/%s", root, include);
        snprintf(to, sizeof(to), "%s/%s/%s", root, folder, name);
        ReplaceChar(from, '\\', '/');
        ReplaceChar(to, '\\', '/');

        if (!FileExists(from))
        {
            continue;
        }

        if (FileExists(to))
        {
            fprintf(stderr, "[Includes] ERROR: %s belongs in %s, but a file of that name is already there. Rename one of them.\n", include, folder);
            continue;
        }

        EnsureParentDirectory(to);

        if (MoveFileA(from, to))
        {
            printf("[Includes] Moved %s into %s, the folder it was added to in Solution Explorer.\n", include, folder);
            List_Add(moved, include);
        }
    }

    Buffer_Free(&text);
}

static void MoveAllFilteredStrays(const char *root)
{
    List filters = {};
    ListMatches(root, "*.vcxproj.filters", &filters);

    for (int i = 0; i < filters.Count; i++)
    {
        MoveFilteredStrays(root, filters.Items[i], &MovedStrays);
    }

    List_Free(&filters);
}

static void EnsureShowAllFiles(const char *projectPath)
{
    char path[MAX_PATH_LENGTH];
    Buffer text = {};
    snprintf(path, sizeof(path), "%s.user", projectPath);

    if (FileExists(path) && (!ReadWholeFile(path, &text) || (text.Data && strstr(text.Data, "<ShowAllFiles>"))))
    {
        Buffer_Free(&text);
        return;
    }

    const char *setting = "  <PropertyGroup>\r\n    <ShowAllFiles>true</ShowAllFiles>\r\n  </PropertyGroup>\r\n";
    char *end = text.Data ? strstr(text.Data, "</Project>") : NULL;
    Buffer updated = {};

    if (end)
    {
        Buffer_Append(&updated, text.Data, (size_t)(end - text.Data));
        Buffer_AppendText(&updated, setting);
        Buffer_AppendText(&updated, end);
    }
    else
    {
        Buffer_AppendText(&updated, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n<Project ToolsVersion=\"Current\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\r\n");
        Buffer_AppendText(&updated, setting);
        Buffer_AppendText(&updated, "</Project>\r\n");
    }

    if (WriteIfChanged(path, &updated))
    {
        printf("[Includes] Turned on Show All Files in Solution Explorer: it takes effect when Visual Studio next loads the project.\n");
    }

    Buffer_Free(&updated);
    Buffer_Free(&text);
}

/* The value of a set "KEY=value" line in a batch file; false when there is none */
static bool FileValue(const char *path, const char *key, Buffer *value)
{
    char pattern[128];
    Buffer file = {};
    snprintf(pattern, sizeof(pattern), "set \"%s=", key);
    const char *start = ReadWholeFile(path, &file) && file.Data ? strstr(file.Data, pattern) : NULL;

    if (start)
    {
        start += strlen(pattern);
        Buffer_Append(value, start, strcspn(start, "\"\r\n"));
    }

    Buffer_Free(&file);
    return start != NULL;
}

/* The platforms Visual Studio builds as their own configuration: every Framework/Automations/Platforms/<Name>/ with a
   Config.bat, except the host one (the one with PLATFORM_ARCHITECTURES), which builds as Debug and Release */
static void ListPlatforms(const char *root, List *names, List *outputs)
{
    char pattern[MAX_PATH_LENGTH];
    snprintf(pattern, sizeof(pattern), "%s/Framework/Automations/Platforms/*", root);
    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA(pattern, &data);

    for (bool more = handle != INVALID_HANDLE_VALUE; more; more = FindNextFileA(handle, &data))
    {
        char path[MAX_PATH_LENGTH];
        Buffer value = {};
        snprintf(path, sizeof(path), "%s/Framework/Automations/Platforms/%s/Config.bat", root, data.cFileName);

        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && data.cFileName[0] != '.' && FileExists(path) && !(FileValue(path, "PLATFORM_ARCHITECTURES", &value) && value.Length > 0))
        {
            Buffer output = {};
            Buffer_Clear(&value);
            FileValue(path, "PLATFORM_OUTPUT", &value);

            for (const char *cursor = value.Data ? value.Data : ""; *cursor; cursor++)
            {
                if (strncmp(cursor, "%PROGRAM_NAME%", 14) == 0)
                {
                    Buffer_AppendText(&output, "$(GameName)");
                    cursor += 13;
                }
                else
                {
                    Buffer_AppendChar(&output, *cursor);
                }
            }

            List_Add(names, data.cFileName);
            List_Add(outputs, output.Data ? output.Data : "");
            Buffer_Free(&output);
        }

        Buffer_Free(&value);
    }

    if (handle != INVALID_HANDLE_VALUE)
    {
        FindClose(handle);
    }
}

/* Replaces what is between the lines holding <!-- marker --> and <!-- End of marker --> */
static bool ReplaceBetweenMarkers(Buffer *text, const char *marker, const Buffer *content)
{
    char open[128];
    char close[128];
    snprintf(open, sizeof(open), "<!-- %s -->", marker);
    snprintf(close, sizeof(close), "<!-- End of %s -->", marker);
    char *start = text->Data ? strstr(text->Data, open) : NULL;
    char *end = start ? strstr(start, close) : NULL;

    if (!end)
    {
        return false;
    }

    start = strchr(start, '\n') + 1;

    while (end > text->Data && end[-1] != '\n')
    {
        end--;
    }

    Buffer updated = {};
    Buffer_Append(&updated, text->Data, (size_t)(start - text->Data));
    Buffer_Append(&updated, content->Data ? content->Data : "", content->Length);
    Buffer_AppendText(&updated, end);
    Buffer_Free(text);
    *text = updated;
    return true;
}

/* Inserts line before the EndGlobalSection that closes the .sln section starting with header, unless it is there */
static void AddSolutionLine(Buffer *solution, const char *header, const char *line)
{
    char *section = solution->Data ? strstr(solution->Data, header) : NULL;
    char *end = section ? strstr(section, "\tEndGlobalSection") : NULL;

    if (!end || (strstr(section, line) && strstr(section, line) < end))
    {
        return;
    }

    Buffer updated = {};
    Buffer_Append(&updated, solution->Data, (size_t)(end - solution->Data));
    Buffer_AppendText(&updated, line);
    Buffer_AppendText(&updated, end);
    Buffer_Free(solution);
    *solution = updated;
}

/* Gives each platform a configuration of its name in the project and in the solutions that hold it (next to it, or a
   folder up), so a platform folder dropped into Platforms appears in Visual Studio's configuration dropdown */
static void SyncPlatforms(const char *root)
{
    List names = {};
    List outputs = {};
    List projects = {};
    ListPlatforms(root, &names, &outputs);
    ListMatches(root, "*.vcxproj", &projects);

    for (int p = 0; p < projects.Count; p++)
    {
        Buffer project = {};
        Buffer configurations = {};
        Buffer properties = {};

        for (int i = 0; i < names.Count; i++)
        {
            char entry[MAX_PATH_LENGTH * 4];
            snprintf(entry, sizeof(entry), "    <ProjectConfiguration Include=\"%s|x64\">\r\n      <Configuration>%s</Configuration>\r\n      <Platform>x64</Platform>\r\n    </ProjectConfiguration>\r\n", names.Items[i], names.Items[i]);
            Buffer_AppendText(&configurations, entry);
            if (outputs.Items[i][0])
            {
                snprintf(entry, sizeof(entry), "  <PropertyGroup Condition=\"'$(Configuration)'=='%s'\">\r\n    <OutputFile>%s</OutputFile>\r\n  </PropertyGroup>\r\n", names.Items[i], outputs.Items[i]);
                Buffer_AppendText(&properties, entry);
            }
        }

        const int updated = UpdatedCount;

        if (ReadWholeFile(projects.Items[p], &project) && ReplaceBetweenMarkers(&project, "Platform configurations", &configurations) && ReplaceBetweenMarkers(&project, "Platform outputs", &properties) && WriteIfChanged(projects.Items[p], &project) && UpdatedCount > updated)
        {
            printf("[Includes] Updated the platform configurations in %s\n", FileName(projects.Items[p]));
        }

        char guid[64] = "";
        List solutions = {};
        ListMatches(root, "*.sln", &solutions);
        char parent[MAX_PATH_LENGTH];
        snprintf(parent, sizeof(parent), "%s", root);
        char *slash = strrchr(parent, '/');

        if (slash)
        {
            *slash = '\0';
            ListMatches(parent, "*.sln", &solutions);
        }

        for (int s = 0; s < solutions.Count; s++)
        {
            Buffer solution = {};
            char quoted[MAX_PATH_LENGTH];
            snprintf(quoted, sizeof(quoted), "%s\", \"{", FileName(projects.Items[p]));
            const char *reference = ReadWholeFile(solutions.Items[s], &solution) && solution.Data ? strstr(solution.Data, quoted) : NULL;

            if (!reference)
            {
                Buffer_Free(&solution);
                continue;
            }

            snprintf(guid, sizeof(guid), "%.38s", reference + strlen(quoted) - 1);
            const size_t before = solution.Length;

            for (int i = 0; i < names.Count; i++)
            {
                char line[MAX_PATH_LENGTH];
                snprintf(line, sizeof(line), "\t\t%s|x64 = %s|x64\r\n", names.Items[i], names.Items[i]);
                AddSolutionLine(&solution, "GlobalSection(SolutionConfigurationPlatforms)", line);
                snprintf(line, sizeof(line), "\t\t%s.%s|x64.ActiveCfg = %s|x64\r\n\t\t%s.%s|x64.Build.0 = %s|x64\r\n", guid, names.Items[i], names.Items[i], guid, names.Items[i], names.Items[i]);
                AddSolutionLine(&solution, "GlobalSection(ProjectConfigurationPlatforms)", line);
            }

            if (solution.Length != before && WriteIfChanged(solutions.Items[s], &solution))
            {
                printf("[Includes] Added the new platforms to %s: reload the solution when Visual Studio asks.\n", FileName(solutions.Items[s]));
            }

            Buffer_Free(&solution);
        }

        List_Free(&solutions);
        Buffer_Free(&project);
        Buffer_Free(&configurations);
        Buffer_Free(&properties);
    }

    List_Free(&projects);
    List_Free(&names);
    List_Free(&outputs);
}

static void SyncProjectItems(const char *root)
{
    char directory[MAX_PATH_LENGTH];
    snprintf(directory, sizeof(directory), "%s/Program", root);

    List files = {};
    List directories = {};
    List items = {};
    List folders = {};
    ListEntries(directory, &files, &directories);
    List_Add(&folders, "Program");

    for (int i = 0; i < files.Count; i++)
    {
        if (!IsPlainName(files.Items[i]))
        {
            printf("[Includes] WARNING: \"%s\" has characters outside plain ASCII. Rename it, or it can't be built or loaded.\n", files.Items[i]);
        }
        else if (IsProjectFile(FileName(files.Items[i])))
        {
            char relative[MAX_PATH_LENGTH];
            snprintf(relative, sizeof(relative), "%s", files.Items[i] + strlen(root) + 1);
            ReplaceChar(relative, '/', '\\');
            List_Add(&items, relative);
        }
    }

    for (int i = 0; i < directories.Count; i++)
    {
        char relative[MAX_PATH_LENGTH];
        snprintf(relative, sizeof(relative), "%s", directories.Items[i] + strlen(root) + 1);
        ReplaceChar(relative, '/', '\\');
        List_Add(&folders, relative);
    }

    qsort(items.Items, (size_t)items.Count, sizeof(char *), CompareInsensitiveItems);
    qsort(folders.Items, (size_t)folders.Count, sizeof(char *), CompareInsensitiveItems);

    List markedItems = {};

    for (int i = 0; i < items.Count; i++)
    {
        char marked[MAX_PATH_LENGTH + 64];
        char uses[MAX_PATH_LENGTH];
        snprintf(marked, sizeof(marked), "%s%s", items.Items[i], UnitUsesHeader(items.Items[i], uses, sizeof(uses)) ? "|unit" : "");
        List_Add(&markedItems, marked);
    }

    List projects = {};
    ListMatches(root, "*.vcxproj", &projects);

    for (int p = 0; p < projects.Count; p++)
    {
        char paths[2][MAX_PATH_LENGTH];
        snprintf(paths[0], sizeof(paths[0]), "%s", projects.Items[p]);
        snprintf(paths[1], sizeof(paths[1]), "%s.filters", projects.Items[p]);
        EnsureShowAllFiles(paths[0]);

        for (int f = 0; f < 2; f++)
        {
            Buffer text = {};
            Buffer stripped = {};
            List existing = {};
            List existingFilters = {};

            if (FileExists(paths[f]) && ReadWholeFile(paths[f], &text) && text.Data)
            {
                RemoveProgramItems(text.Data, &stripped, &existing, &existingFilters, &MovedStrays);

                if (MovedStrays.Count > 0 || !SameEntries(&existing, f == 0 ? &markedItems : &items) || (f == 1 && !SameEntries(&existingFilters, &folders)))
                {
                    Buffer block = {};
                    Buffer_AppendText(&block, "  <ItemGroup>\r\n");

                    for (int i = 0; f == 1 && i < folders.Count; i++)
                    {
                        const unsigned int hash = HashPath(folders.Items[i]);
                        char entry[MAX_PATH_LENGTH + 128];
                        snprintf(entry, sizeof(entry), "    <Filter Include=\"%s\">\r\n      <UniqueIdentifier>{%08X-%04X-4%03X-8%03X-%08X%04X}</UniqueIdentifier>\r\n    </Filter>\r\n",
                                 folders.Items[i], hash, hash >> 16, hash & 0xFFF, (hash >> 8) & 0xFFF, hash * 2654435761u, hash & 0xFFFF);
                        Buffer_AppendText(&block, entry);
                    }

                    for (int i = 0; i < items.Count; i++)
                    {
                        char entry[MAX_PATH_LENGTH * 3 + 128];
                        char uses[MAX_PATH_LENGTH];
                        const char *type = ProjectItemType(items.Items[i]);

                        if (f == 0 && UnitUsesHeader(items.Items[i], uses, sizeof(uses)))
                        {
                            snprintf(entry, sizeof(entry), "    <%s Include=\"%s\">\r\n      <ForcedIncludeFiles>$(ProjectDir)%s</ForcedIncludeFiles>\r\n    </%s>\r\n", type, items.Items[i], uses, type);
                        }
                        else if (f == 0)
                        {
                            snprintf(entry, sizeof(entry), "    <%s Include=\"%s\" />\r\n", type, items.Items[i]);
                        }
                        else
                        {
                            const char *slash = strrchr(items.Items[i], '\\');
                            snprintf(entry, sizeof(entry), "    <%s Include=\"%s\">\r\n      <Filter>%.*s</Filter>\r\n    </%s>\r\n", type, items.Items[i], (int)(slash - items.Items[i]), items.Items[i], type);
                        }

                        Buffer_AppendText(&block, entry);
                    }

                    Buffer_AppendText(&block, "  </ItemGroup>\r\n");
                    InsertBeforeProjectEnd(&stripped, &block);

                    if (WriteIfChanged(paths[f], &stripped))
                    {
                        printf("[Includes] Updated the files listed in %s\n", FileName(paths[f]));
                    }

                    Buffer_Free(&block);
                }
            }

            Buffer_Free(&text);
            Buffer_Free(&stripped);
            List_Free(&existing);
            List_Free(&existingFilters);
        }
    }

    List_Free(&projects);
    List_Free(&files);
    List_Free(&directories);
    List_Free(&markedItems);
    List_Free(&items);
    List_Free(&folders);
}

static void LockProject(const char *root, const char *kind)
{
    char name[64];
    snprintf(name, sizeof(name), "Local\\Framework%s%08X", kind, HashPath(root));
    HANDLE mutex = CreateMutexA(NULL, FALSE, name);

    if (mutex)
    {
        WaitForSingleObject(mutex, 60000);
    }
}

static bool CheckScriptLocations(const char *root)
{
    List files = {};
    char directory[MAX_PATH_LENGTH];
    ListMatches(root, "*", &files);
    snprintf(directory, sizeof(directory), "%s/Program", root);
    ListEntries(directory, &files, NULL);
    bool succeeded = true;

    for (int i = 0; i < files.Count; i++)
    {
        const char *relative = files.Items[i] + strlen(root) + 1;
        const bool code = IsCodeFile(relative) || EndsWithExtension(relative, ".c");
        const bool placed = _strnicmp(relative, "Program/Code/Types/", 19) == 0 || _strnicmp(relative, "Program/Code/Datas/", 19) == 0 ||
                            _strnicmp(relative, "Program/Code/Systems/", 21) == 0 || _stricmp(relative, "Program/Code/Program.cpp") == 0 ||
                            _strnicmp(relative, "Program/Assets/", 15) == 0;

        if (code && !placed)
        {
            fprintf(stderr, "[Includes] ERROR: %s is not in Program/Code/Types, Datas or Systems, so no script can see it. Move it into one of them. (Visual Studio puts new files in the project folder when Show All Files is off in Solution Explorer.)\n", relative);
            succeeded = false;
        }
    }

    List_Free(&files);
    return succeeded;
}

/* The value of a set "KEY=value" line in Config.bat; false when there is none */
static bool ConfigValue(const char *root, const char *key, Buffer *value)
{
    char path[MAX_PATH_LENGTH];
    snprintf(path, sizeof(path), "%s/Config.bat", root);
    return FileValue(path, key, value);
}

/* #define KEY "value" from Config.bat, or from fallback; nothing when both are missing */
static void AppendConfigDefine(Buffer *output, const char *root, const char *key, const char *fallback)
{
    Buffer value = {};
    const char *text = ConfigValue(root, key, &value) ? value.Data : fallback;

    if (text)
    {
        Buffer_AppendText(output, "#define ");
        Buffer_AppendText(output, key);
        Buffer_AppendText(output, " \"");

        for (const char *cursor = text; *cursor; cursor++)
        {
            if (*cursor == '"' || *cursor == '\\')
            {
                Buffer_AppendChar(output, '\\');
            }

            Buffer_AppendChar(output, *cursor);
        }

        Buffer_AppendText(output, "\"\r\n");
    }

    Buffer_Free(&value);
}

static int IncludesCommand(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "[Includes] Usage: Tools Includes <project root> [--project]\n");
        return 1;
    }

    char root[MAX_PATH_LENGTH];
    snprintf(root, sizeof(root), "%s", argv[1]);
    NormalizePath(root);
    LockProject(root, "Headers");

    char programDirectory[MAX_PATH_LENGTH];
    char frameworkDirectory[MAX_PATH_LENGTH];
    char includesDirectory[MAX_PATH_LENGTH];
    char typesDirectory[MAX_PATH_LENGTH];
    char datasDirectory[MAX_PATH_LENGTH];
    char systemsDirectory[MAX_PATH_LENGTH];

    snprintf(programDirectory, sizeof(programDirectory), "%s/Program/Code", root);
    snprintf(frameworkDirectory, sizeof(frameworkDirectory), "%s/Framework", root);
    snprintf(includesDirectory, sizeof(includesDirectory), "%s/Framework/Includes", root);
    snprintf(typesDirectory, sizeof(typesDirectory), "%s/Program/Code/Types", root);
    snprintf(datasDirectory, sizeof(datasDirectory), "%s/Program/Code/Datas", root);
    snprintf(systemsDirectory, sizeof(systemsDirectory), "%s/Program/Code/Systems", root);

    if (!DirectoryExists(programDirectory))
    {
        fprintf(stderr, "[Includes] ERROR: Could not find \"%s\"\n", programDirectory);
        return 1;
    }

    if (!DirectoryExists(frameworkDirectory))
    {
        fprintf(stderr, "[Includes] ERROR: Could not find \"%s\"\n", frameworkDirectory);
        return 1;
    }

    EnsureDirectory(includesDirectory);

    const bool syncProject = argc > 2 && strcmp(argv[2], "--project") == 0;

    if (syncProject)
    {
        MoveAllFilteredStrays(root);
    }

    const bool systemHeadersSucceeded = ScanSystems(systemsDirectory);

    char usesDirectory[MAX_PATH_LENGTH];
    snprintf(usesDirectory, sizeof(usesDirectory), "%s/Uses", includesDirectory);
    const bool usesSucceeded = GenerateUsesHeaders(programDirectory, systemsDirectory, usesDirectory);

    Buffer types = {};
    Buffer datas = {};

    Buffer_AppendText(&types, "#pragma once\r\n\r\n");
    Buffer_AppendText(&datas, "#pragma once\r\n\r\n");

    char signature[96];
    snprintf(signature, sizeof(signature), "#define FRAMEWORK_STATE_SIGNATURE 0x%08Xu\r\n", StateSignature(root));
    Buffer_AppendText(&types, signature);
    AppendConfigDefine(&types, root, "PROGRAM_NAME", "Program");
    AppendConfigDefine(&types, root, "PROGRAM_VERSION", "1.0.0");
    AppendConfigDefine(&types, root, "PROGRAM_IDENTIFIER", NULL);
    Buffer_AppendText(&types, "\r\n");
    EmitSection(&types, typesDirectory, programDirectory, "Program/Code", "Program.hpp");

    char datasCopiesDirectory[MAX_PATH_LENGTH];
    snprintf(datasCopiesDirectory, sizeof(datasCopiesDirectory), "%s/Datas", includesDirectory);
    const bool datasSucceeded = GenerateDatas(datasDirectory, datasCopiesDirectory, &datas);

    char typesPath[MAX_PATH_LENGTH];
    char forwardDeclarationsPath[MAX_PATH_LENGTH];
    char datasPath[MAX_PATH_LENGTH];

    snprintf(typesPath, sizeof(typesPath), "%s/Types.hpp", includesDirectory);
    snprintf(forwardDeclarationsPath, sizeof(forwardDeclarationsPath), "%s/ForwardDeclarations.hpp", includesDirectory);
    snprintf(datasPath, sizeof(datasPath), "%s/Datas.hpp", includesDirectory);

    bool aggregatesSucceeded = WriteIfChanged(typesPath, &types);
    aggregatesSucceeded &= WriteForwardDeclarations(forwardDeclarationsPath);
    aggregatesSucceeded &= WriteIfChanged(datasPath, &datas);

    Buffer_Free(&types);
    Buffer_Free(&datas);

    const int headerCount = UpdatedCount;

    if (syncProject)
    {
        SyncProjectItems(root);
        SyncPlatforms(root);
    }

    UpdatedCount = headerCount;

    if (UpdatedCount > 0)
    {
        printf("[Includes] Regenerated %d include header(s) at \"%s\"\n", UpdatedCount, includesDirectory);
    }
    else
    {
        printf("[Includes] Include headers are up to date.\n");
    }

    const bool locationsSucceeded = CheckScriptLocations(root);
    return (systemHeadersSucceeded && usesSucceeded && aggregatesSucceeded && datasSucceeded && locationsSucceeded) ? 0 : 1;
}

#pragma region Compile
/* Tools Compile: compiles the units whose sources or headers changed, in parallel, and lists the objects */

typedef struct
{
    Job Job;
    char Source[MAX_PATH_LENGTH];
    char Object[MAX_PATH_LENGTH];
    char Dependencies[MAX_PATH_LENGTH];
    char Uses[MAX_PATH_LENGTH];
    char UsesPath[MAX_PATH_LENGTH];
    int Precompiled;
} Unit;

enum
{
    PRECOMPILED_NONE,
    PRECOMPILED_USE,
    PRECOMPILED_CREATE
};

typedef struct
{
    Unit *Items;
    int Count;
    int Capacity;
} Units;

static char PrecompiledHeader[MAX_PATH_LENGTH];
static char PrecompiledFile[MAX_PATH_LENGTH];

/* --gnu: the compiler takes GCC-style flags (clang, zig c++, emcc, the Android NDK's clang++) instead of clang-cl's */
static bool GnuStyle = false;
static char GnuRoot[MAX_PATH_LENGTH];

/* path, with forward slashes, made absolute against the project root when it is relative */
static void AppendGnuPath(Buffer *command, const char *path)
{
    char argument[MAX_PATH_LENGTH * 2];
    const bool relative = path[0] && path[0] != '/' && path[0] != '\\' && path[1] != ':';
    snprintf(argument, sizeof(argument), relative ? "%s/%s" : "%s%s", relative ? GnuRoot : "", path);
    ReplaceChar(argument, '\\', '/');
    AppendArgument(command, argument);
}

static int MissingUses = 0;

static Unit *Units_Add(Units *units, const char *root, const char *objectsDirectory, const char *source, int precompiled)
{
    if (units->Count == units->Capacity)
    {
        units->Capacity = units->Capacity ? units->Capacity * 2 : 16;
        units->Items = (Unit *)realloc(units->Items, units->Capacity * sizeof(Unit));

        if (!units->Items)
        {
            OutOfMemory();
        }
    }

    Unit *unit = &units->Items[units->Count++];
    *unit = Unit{};
    unit->Precompiled = precompiled;

    const size_t rootLength = strlen(root);
    const size_t objectsLength = strlen(objectsDirectory);
    const char *name = source;

    if (_strnicmp(source, objectsDirectory, objectsLength) == 0 && source[objectsLength] == '/')
    {
        name = source + objectsLength + 1;
    }
    else if (_strnicmp(source, root, rootLength) == 0 && source[rootLength] == '/')
    {
        name = source + rootLength + 1;
    }

    snprintf(unit->Job.Name, sizeof(unit->Job.Name), "%s", name);

    char stem[MAX_PATH_LENGTH];
    snprintf(stem, sizeof(stem), "%s/%s", objectsDirectory, name);

    snprintf(unit->Source, sizeof(unit->Source), "%s", source);
    snprintf(unit->Object, sizeof(unit->Object), GnuStyle ? "%s.o" : "%s.obj", stem);
    snprintf(unit->Dependencies, sizeof(unit->Dependencies), "%s.d", stem);
    snprintf(unit->Job.Log, sizeof(unit->Job.Log), "%s.log", stem);

    char systems[MAX_PATH_LENGTH];
    char uses[MAX_PATH_LENGTH];
    snprintf(systems, sizeof(systems), "%s/Program/Code/Systems/", root);
    uses[0] = '\0';

    if (_strnicmp(source, systems, strlen(systems)) == 0)
    {
        snprintf(uses, sizeof(uses), "Framework/Includes/Uses/Systems/%s", source + strlen(systems));
    }
    else if (_stricmp(name, "Program/Code/Program.cpp") == 0)
    {
        snprintf(uses, sizeof(uses), "Framework/Includes/Uses/Program.hpp");
    }

    char usesPath[MAX_PATH_LENGTH];
    snprintf(usesPath, sizeof(usesPath), "%s/%s", root, uses);

    if (uses[0] && FileExists(usesPath))
    {
        snprintf(unit->Uses, sizeof(unit->Uses), "%s", uses);
        snprintf(unit->UsesPath, sizeof(unit->UsesPath), "%s", usesPath);
    }
    else if (uses[0])
    {
        fprintf(stderr, "[Compile] ERROR: %s is missing, so %s can't see the other scripts. The Includes step did not run or failed: build again.\n", uses, name);
        MissingUses++;
    }

    return unit;
}

static bool IsUpToDate(const Unit *unit, ULONGLONG notOlderThan)
{
    ULONGLONG objectTime = 0;
    Buffer text = {};

    if (!GetWriteTime(unit->Object, &objectTime) || objectTime < notOlderThan || !ReadWholeFile(unit->Dependencies, &text))
    {
        Buffer_Free(&text);
        return false;
    }

    const char *cursor = text.Data ? text.Data : "";
    const char *end = cursor + text.Length;

    while (cursor < end && !(cursor[0] == ':' && (cursor + 1 == end || IsWhitespace(cursor[1]))))
    {
        cursor++;
    }

    bool upToDate = cursor < end;
    bool any = false;
    Buffer path = {};

    for (cursor++; upToDate && cursor <= end; cursor++)
    {
        const char character = cursor < end ? *cursor : '\n';
        const char next = cursor + 1 < end ? cursor[1] : '\n';

        if (character == '\\' && (next == ' ' || next == '#'))
        {
            Buffer_AppendChar(&path, next);
            cursor++;
        }
        else if (character == '$' && next == '$')
        {
            Buffer_AppendChar(&path, '$');
            cursor++;
        }
        else if (IsWhitespace(character) || (character == '\\' && (next == '\r' || next == '\n')))
        {
            if (path.Length > 0)
            {
                ULONGLONG dependencyTime = 0;
                any = true;
                upToDate = GetWriteTime(path.Data, &dependencyTime) && dependencyTime <= objectTime;
                Buffer_Clear(&path);
            }
        }
        else
        {
            Buffer_AppendChar(&path, character);
        }
    }

    Buffer_Free(&path);
    Buffer_Free(&text);
    return upToDate && any;
}

static void BuildUnitCommand(Unit *unit, const Buffer *baseCommand)
{
    EnsureParentDirectory(unit->Object);
    Buffer *command = &unit->Job.Command;
    Buffer_Clear(command);
    Buffer_Append(command, baseCommand->Data, baseCommand->Length);

    if (GnuStyle)
    {
        AppendArgument(command, "-c");
        AppendArgument(command, "-MD");
        AppendArgument(command, "-MF");
        AppendGnuPath(command, unit->Dependencies);
        AppendArgument(command, "-o");
        AppendGnuPath(command, unit->Object);

        if (PrecompiledHeader[0])
        {
            AppendArgument(command, "-include");
            AppendGnuPath(command, PrecompiledHeader);
        }

        if (unit->Uses[0])
        {
            AppendArgument(command, "-include");
            AppendGnuPath(command, unit->Uses);
        }

        AppendArgument(command, "-x");
        AppendArgument(command, "c++");
        AppendGnuPath(command, unit->Source);
        return;
    }

    AppendArgument(command, "/c");
    AppendArgument(command, "/TP");
    AppendArgument(command, "/clang:-MD");
    AppendPathArgument(command, "/clang:-MF", unit->Dependencies);
    AppendPathArgument(command, "/Fo", unit->Object);

    if (unit->Precompiled != PRECOMPILED_NONE)
    {
        AppendPathArgument(command, unit->Precompiled == PRECOMPILED_CREATE ? "/Yc" : "/Yu", PrecompiledHeader);
        AppendPathArgument(command, "/FI", PrecompiledHeader);
        AppendPathArgument(command, "/Fp", PrecompiledFile);
    }

    if (unit->Uses[0])
    {
        AppendPathArgument(command, "/FI", unit->Uses);
    }

    AppendPathArgument(command, "", unit->Source);
}

static void PrintHints(const Unit *unit)
{
    Buffer hints = {};

    if (unit->UsesPath[0] && ReadWholeFile(unit->UsesPath, &hints) && hints.Data)
    {
        for (char *line = strtok(hints.Data, "\r\n"); line; line = strtok(NULL, "\r\n"))
        {
            if (strncmp(line, "// HINT: ", 9) == 0)
            {
                printf("[Compile] HINT: %s\n", line + 9);
            }
        }
    }

    Buffer_Free(&hints);

    if (strstr(unit->Job.Name, "IntelliSenseView.cpp"))
    {
        printf("[Compile] HINT: The declarations shared between Systems scripts don't compile together. The error above names the line; make that declaration static, or move it to a Types file.\n");
    }
}

static void UnitFinished(Job *job)
{
    Unit *unit = (Unit *)job;

    if (!job->Failed)
    {
        return;
    }

    DeleteFileA(unit->Object);
    DeleteFileA(unit->Dependencies);

    if (unit->Precompiled == PRECOMPILED_CREATE)
    {
        DeleteFileA(PrecompiledFile);
    }

    printf("[Compile] ERROR: %s failed to compile.\n", job->Name);
    PrintHints(unit);
    fflush(stdout);
}

static int CompileCommand(int argc, char *argv[])
{
    int separator = 0;

    for (int i = 1; i < argc && !separator; i++)
    {
        separator = strcmp(argv[i], "--") == 0 ? i : 0;
    }

    if (separator < 3 || separator + 1 >= argc)
    {
        fprintf(stderr, "[Compile] Usage: Tools Compile <project root> <objects folder> [extra sources...] [--pch <header>] [--gnu] -- <compiler> [flags...]\n");
        return 1;
    }

    char root[MAX_PATH_LENGTH];
    char objectsDirectory[MAX_PATH_LENGTH];
    snprintf(root, sizeof(root), "%s", argv[1]);
    snprintf(objectsDirectory, sizeof(objectsDirectory), "%s", argv[2]);
    NormalizePath(root);
    NormalizePath(objectsDirectory);
    EnsureDirectory(objectsDirectory);
    LockProject(root, "Headers");

    Buffer baseCommand = {};
    List extras = {};

    for (int i = separator + 1; i < argc; i++)
    {
        AppendArgument(&baseCommand, argv[i]);
    }

    for (int i = 3; i < separator; i++)
    {
        if (strcmp(argv[i], "--gnu") == 0)
        {
            GnuStyle = true;
            snprintf(GnuRoot, sizeof(GnuRoot), "%s", root);
        }
        else if (strcmp(argv[i], "--pch") == 0 && i + 1 < separator)
        {
            snprintf(PrecompiledHeader, sizeof(PrecompiledHeader), "%s", argv[++i]);
            snprintf(PrecompiledFile, sizeof(PrecompiledFile), "%s/Precompiled.pch", objectsDirectory);
        }
        else
        {
            char extra[MAX_PATH_LENGTH];
            snprintf(extra, sizeof(extra), "%s", argv[i]);
            NormalizePath(extra);
            List_Add(&extras, extra);
        }
    }

    /* GCC-style compilers get the precompiled header's includes forced in, without precompiling them */
    const bool usePrecompiled = PrecompiledHeader[0] != '\0' && !GnuStyle;
    const int programPrecompiled = usePrecompiled ? PRECOMPILED_USE : PRECOMPILED_NONE;

    Units units = {};
    const Buffer empty = {};
    Buffer view = {};
    char path[MAX_PATH_LENGTH];
    Buffer_AppendText(&view, "#include <Framework/Framework.hpp>\r\n");

    if (usePrecompiled)
    {
        snprintf(path, sizeof(path), "%s/Precompiled.cpp", objectsDirectory);
        WriteIfChanged(path, &empty);
        Units_Add(&units, root, objectsDirectory, path, PRECOMPILED_CREATE);
    }

    List scripts = {};
    snprintf(path, sizeof(path), "%s/Program/Code/Systems", root);
    ListEntries(path, &scripts, NULL);
    qsort(scripts.Items, (size_t)scripts.Count, sizeof(char *), CompareInsensitiveItems);
    snprintf(path, sizeof(path), "%s/Program/Code/Program.cpp", root);
    List_Add(&scripts, path);

    for (int i = 0; i < scripts.Count; i++)
    {
        if (IsCodeFile(scripts.Items[i]) || i == scripts.Count - 1)
        {
            Units_Add(&units, root, objectsDirectory, scripts.Items[i], programPrecompiled);
        }
    }

    snprintf(path, sizeof(path), "%s/IntelliSenseView.cpp", objectsDirectory);
    WriteIfChanged(path, &view);
    Units_Add(&units, root, objectsDirectory, path, programPrecompiled);

    for (int i = 0; i < extras.Count; i++)
    {
        Units_Add(&units, root, objectsDirectory, extras.Items[i], PRECOMPILED_NONE);
    }

    List_Free(&scripts);
    List_Free(&extras);
    Buffer_Free(&view);

    if (MissingUses > 0)
    {
        return 1;
    }

    Buffer signature = {};
    Buffer previousSignature = {};
    Buffer_Append(&signature, baseCommand.Data, baseCommand.Length);
    AppendArgument(&signature, PrecompiledHeader);

    char flagsPath[MAX_PATH_LENGTH];
    snprintf(flagsPath, sizeof(flagsPath), "%s/Flags.txt", objectsDirectory);
    const bool sameFlags = ReadWholeFile(flagsPath, &previousSignature) && previousSignature.Length == signature.Length && memcmp(previousSignature.Data, signature.Data, signature.Length) == 0;
    Buffer_Free(&previousSignature);

    if (!sameFlags)
    {
        DeleteFileA(flagsPath);
    }

    for (int i = 0; i < units.Count; i++)
    {
        BuildUnitCommand(&units.Items[i], &baseCommand);
    }

    int staleCount = 0;
    bool failed = false;
    ULONGLONG precompiledTime = 0;

    if (usePrecompiled)
    {
        Unit *precompiled = &units.Items[0];
        Job *job = &precompiled->Job;

        if (!sameFlags || !IsUpToDate(precompiled, 0) || !GetWriteTime(PrecompiledFile, &precompiledTime))
        {
            staleCount++;
            failed = !RunJobs(&job, 1, UnitFinished);
        }

        GetWriteTime(PrecompiledFile, &precompiledTime);
    }

    Job **stale = (Job **)malloc(sizeof(Job *) * (size_t)(units.Count + 1));
    int count = 0;

    if (!stale)
    {
        OutOfMemory();
    }

    for (int i = usePrecompiled ? 1 : 0; i < units.Count; i++)
    {
        Unit *unit = &units.Items[i];

        if (!sameFlags || !IsUpToDate(unit, unit->Precompiled == PRECOMPILED_USE ? precompiledTime : 0))
        {
            stale[count++] = &unit->Job;
        }
    }

    staleCount += count;
    failed = failed || !RunJobs(stale, count, UnitFinished);
    free(stale);

    Buffer linkList = {};
    List expected = {};
    List leftovers = {};

    for (int i = 0; i < units.Count; i++)
    {
        char object[MAX_PATH_LENGTH];
        snprintf(object, sizeof(object), "\"%s\"\r\n", units.Items[i].Object);
        ReplaceChar(object, GnuStyle ? '\\' : '/', GnuStyle ? '/' : '\\');
        Buffer_AppendText(&linkList, object);
        List_Add(&expected, units.Items[i].Object);
        List_Add(&expected, units.Items[i].Dependencies);
        Buffer_Free(&units.Items[i].Job.Command);
    }

    ListEntries(objectsDirectory, &leftovers, NULL);

    for (int i = 0; i < leftovers.Count; i++)
    {
        const char *leftover = leftovers.Items[i];

        if ((EndsWithExtension(leftover, ".obj") || EndsWithExtension(leftover, ".o") || EndsWithExtension(leftover, ".d")) && !List_ContainsInsensitive(&expected, leftover))
        {
            DeleteFileA(leftover);
        }
    }

    snprintf(path, sizeof(path), "%s/Objects.rsp", objectsDirectory);

    if (!WriteIfChanged(path, &linkList) || (!failed && !sameFlags && !WriteIfChanged(flagsPath, &signature)))
    {
        fprintf(stderr, "[Compile] ERROR: Could not write to \"%s\"\n", objectsDirectory);
        failed = true;
    }

    if (!failed)
    {
        printf("[Compile] %d of %d unit(s) compiled, the rest were up to date.\n", staleCount, units.Count);
    }

    Buffer_Free(&linkList);
    Buffer_Free(&baseCommand);
    Buffer_Free(&signature);
    List_Free(&expected);
    List_Free(&leftovers);
    free(units.Items);
    return failed ? 1 : 0;
}

#pragma endregion

#pragma region Shaders
/* Tools Shaders: compiles the changed .hlsl files to each format a platform asks for */

#define MAX_INCLUDE_DEPTH 32

/* Follows a shader's #include "..." and #include <...>, each file once: the newest write time among them (the
   largest value when one is missing), and when output is given, the source with each #include "..." pasted in */
static bool FollowIncludes(const char *path, const List *includeDirectories, List *visited, int depth, ULONGLONG *newest, Buffer *output)
{
    ULONGLONG time = ~(ULONGLONG)0;
    Buffer text = {};
    List_Add(visited, path);
    const bool read = depth < MAX_INCLUDE_DEPTH && GetWriteTime(path, &time) && ReadWholeFile(path, &text);
    *newest = time > *newest ? time : *newest;
    bool succeeded = read;

    if (!read && output)
    {
        fprintf(stderr, "[Shaders] ERROR: Could not read \"%s\"\n", path);
    }

    char directory[MAX_PATH_LENGTH];
    snprintf(directory, sizeof(directory), "%s", path);
    *strrchr(directory, '/') = '\0';

    for (char *line = text.Data, *next; line && *line; line = next)
    {
        char name[MAX_PATH_LENGTH];
        char quote = 0;
        next = strchr(line, '\n');
        const size_t length = next ? (size_t)(++next - line) : strlen(line);
        const bool include = sscanf(line, " # include %c%1000[^>\"\r\n]", &quote, name) == 2 && (quote == '"' || quote == '<');

        if (output && !(include && quote == '"'))
        {
            Buffer_Append(output, line, length);
        }

        if (!include)
        {
            continue;
        }

        char included[MAX_PATH_LENGTH];
        NormalizePath(name);
        snprintf(included, sizeof(included), "%s/%s", directory, name);

        for (int i = 0; i < includeDirectories->Count && !FileExists(included); i++)
        {
            snprintf(included, sizeof(included), "%s/%s", includeDirectories->Items[i], name);
        }

        if (!List_ContainsInsensitive(visited, included) && !FollowIncludes(included, includeDirectories, visited, depth + 1, newest, quote == '"' ? output : NULL))
        {
            succeeded = succeeded && quote == '<';
        }
    }

    if (output && depth > 0 && output->Length > 0 && output->Data[output->Length - 1] != '\n')
    {
        Buffer_AppendChar(output, '\n');
    }

    Buffer_Free(&text);
    return succeeded;
}

static bool WriteWholeFile(const char *path, const Buffer *text)
{
    FILE *file = fopen(path, "wb");
    const bool written = file && (text->Length == 0 || fwrite(text->Data, 1, text->Length, file) == text->Length);
    return file && fclose(file) == 0 && written;
}

typedef struct
{
    Job Job;
    char Output[MAX_PATH_LENGTH];
} ShaderJob;

static void ShaderFinished(Job *job)
{
    if (job->Failed)
    {
        DeleteFileA(((ShaderJob *)job)->Output);
        printf("[Shaders] ERROR: %s failed to compile.\n", job->Name);
    }
    else
    {
        printf("[Shaders] %s\n", job->Name);
    }

    fflush(stdout);
}

static const struct { const char *Suffix; const char *Profile; const char *Stage; } ShaderStages[] = {
    {".vert.hlsl", "vs_6_0", "vertex"},
    {".frag.hlsl", "ps_6_0", "fragment"},
    {".comp.hlsl", "cs_6_0", "compute"},
    {".vert.glsl", "vert", "vertex"},
    {".frag.glsl", "frag", "fragment"},
    {".comp.glsl", "comp", "compute"},
};

static bool IsShaderSource(const char *path)
{
    bool source = EndsWithExtension(path, ".hlsli") || EndsWithExtension(path, ".glsli");

    for (size_t s = 0; s < sizeof(ShaderStages) / sizeof(ShaderStages[0]); s++)
    {
        source = source || EndsWithExtension(path, ShaderStages[s].Suffix);
    }

    return source;
}

static void ExpandShaderArgument(const char *pattern, char *expanded, size_t size, const char *const names[4], const char *const values[4])
{
    size_t length = 0;

    while (*pattern && length + 1 < size)
    {
        int match = -1;

        for (int i = 0; i < 4 && match < 0; i++)
        {
            match = strncmp(pattern, names[i], strlen(names[i])) == 0 ? i : -1;
        }

        if (match < 0)
        {
            expanded[length++] = *pattern++;
            continue;
        }

        length += (size_t)snprintf(expanded + length, size - length, "%s", values[match]);
        length = length < size ? length : size - 1;
        pattern += strlen(names[match]);
    }

    expanded[length] = '\0';
}

static int ShadersCommand(int argc, char *argv[])
{
    struct { const char *Extension; int First; int Count; } formats[16];
    int formatCount = 0;

    if (argc < 4)
    {
        fprintf(stderr, "[Shaders] Usage: Tools Shaders <assets folder> <output folder> <state file> [--format <extension> <compiler> <arguments with {input} {output} {profile} {stage} {include}>]...\n");
        return 1;
    }

    char assetsDirectory[MAX_PATH_LENGTH];
    char outputDirectory[MAX_PATH_LENGTH];
    char statePath[MAX_PATH_LENGTH];
    snprintf(assetsDirectory, sizeof(assetsDirectory), "%s", argv[1]);
    snprintf(outputDirectory, sizeof(outputDirectory), "%s", argv[2]);
    snprintf(statePath, sizeof(statePath), "%s", argv[3]);
    NormalizePath(assetsDirectory);
    NormalizePath(outputDirectory);
    NormalizePath(statePath);

    Buffer signature = {};

    for (int i = 4; i < argc; i++)
    {
        AppendArgument(&signature, argv[i]);

        if (strcmp(argv[i], "--format") == 0 && i + 2 < argc && formatCount < 16)
        {
            formats[formatCount].Extension = argv[i + 1];
            formats[formatCount].First = i + 2;
            formats[formatCount].Count = 0;

            while (i + 2 + formats[formatCount].Count < argc && strcmp(argv[i + 2 + formats[formatCount].Count], "--format") != 0)
            {
                formats[formatCount].Count++;
            }

            formatCount++;
        }
    }

    Buffer state = {};
    bool sameSignature = false;

    if (ReadWholeFile(statePath, &state) && state.Data)
    {
        char *line = strtok(state.Data, "\r\n");
        sameSignature = line && signature.Data && strcmp(line, signature.Data) == 0;

        for (line = strtok(NULL, "\r\n"); line; line = strtok(NULL, "\r\n"))
        {
            char source[MAX_PATH_LENGTH];
            char output[MAX_PATH_LENGTH];
            char glslSource[MAX_PATH_LENGTH];
            snprintf(source, sizeof(source), "%s/%s", assetsDirectory, line);
            snprintf(output, sizeof(output), "%s/%s", outputDirectory, line);
            *strrchr(source, '.') = '\0';
            snprintf(glslSource, sizeof(glslSource), "%s.glsl", source);
            strncat(source, ".hlsl", sizeof(source) - strlen(source) - 1);

            if (!sameSignature || (!FileExists(source) && !FileExists(glslSource)))
            {
                DeleteFileA(output);
            }
        }
    }

    Buffer_Free(&state);

    List folders = {};
    ListEntries(outputDirectory, NULL, &folders);

    for (int i = folders.Count - 1; i >= 0; i--)
    {
        RemoveDirectoryA(folders.Items[i]);
    }

    List_Free(&folders);

    List sources = {};
    ListEntries(assetsDirectory, &sources, NULL);

    ShaderJob *jobs = (ShaderJob *)calloc((size_t)sources.Count * (size_t)formatCount + 1, sizeof(ShaderJob));
    Job **stale = (Job **)calloc((size_t)sources.Count * (size_t)formatCount + 1, sizeof(Job *));
    Buffer newState = {};
    int count = 0;
    int copied = 0;
    int shaderCount = 0;
    bool succeeded = true;

    if (!jobs || !stale)
    {
        OutOfMemory();
    }

    Buffer_AppendText(&newState, signature.Data);
    Buffer_AppendText(&newState, "\r\n");

    for (int i = 0; i < sources.Count; i++)
    {
        const char *source = sources.Items[i];
        int stage = -1;

        for (int s = 0; s < (int)(sizeof(ShaderStages) / sizeof(ShaderStages[0])); s++)
        {
            stage = EndsWithExtension(source, ShaderStages[s].Suffix) ? s : stage;
        }

        if (stage < 0)
        {
            continue;
        }

        shaderCount++;
        List visited = {};
        List includeDirectories = {};
        char ancestor[MAX_PATH_LENGTH];
        snprintf(ancestor, sizeof(ancestor), "%s", source);
        *strrchr(ancestor, '/') = '\0';

        while (strlen(ancestor) > strlen(assetsDirectory) && strrchr(ancestor, '/'))
        {
            *strrchr(ancestor, '/') = '\0';
            List_Add(&includeDirectories, ancestor);
        }

        ULONGLONG newestInput = 0;
        FollowIncludes(source, &includeDirectories, &visited, 0, &newestInput, NULL);
        List_Free(&visited);

        const char *relative = source + strlen(assetsDirectory) + 1;
        const int stemLength = (int)(strlen(relative) - strlen(".hlsl"));

        for (int f = 0; f < formatCount; f++)
        {
            ShaderJob *job = &jobs[count];
            ULONGLONG outputTime = 0;
            snprintf(job->Job.Name, sizeof(job->Job.Name), "%.*s%s", stemLength, relative, formats[f].Extension);
            snprintf(job->Output, sizeof(job->Output), "%s/%s", outputDirectory, job->Job.Name);
            Buffer_AppendText(&newState, job->Job.Name);
            Buffer_AppendText(&newState, "\r\n");

            char oldOutput[MAX_PATH_LENGTH];
            snprintf(oldOutput, sizeof(oldOutput), "%s/%s", assetsDirectory, job->Job.Name);
            DeleteFileA(oldOutput);

            if (sameSignature && GetWriteTime(job->Output, &outputTime) && outputTime >= newestInput)
            {
                continue;
            }

            if (formats[f].Count == 1 && strcmp(argv[formats[f].First], "copy") == 0)
            {
                EnsureParentDirectory(job->Output);
                List included = {};
                Buffer text = {};
                ULONGLONG ignored = 0;
                const bool copiedNow = FollowIncludes(source, &includeDirectories, &included, 0, &ignored, &text) && WriteWholeFile(job->Output, &text);
                List_Free(&included);
                Buffer_Free(&text);
                printf(copiedNow ? "[Shaders] %s\n" : "[Shaders] ERROR: %s could not be copied.\n", job->Job.Name);
                succeeded = succeeded && copiedNow;
                copied++;
                continue;
            }

            snprintf(job->Job.Log, sizeof(job->Job.Log), "%s.%d.log", statePath, count);
            char input[MAX_PATH_LENGTH];
            char output[MAX_PATH_LENGTH];
            snprintf(input, sizeof(input), "%s", source);
            snprintf(output, sizeof(output), "%s", job->Output);
            ReplaceChar(input, '/', '\\');
            ReplaceChar(output, '/', '\\');
            const char *const names[4] = {"{input}", "{output}", "{profile}", "{stage}"};
            const char *const values[4] = {input, output, ShaderStages[stage].Profile, ShaderStages[stage].Stage};

            for (int a = formats[f].First; a < formats[f].First + formats[f].Count; a++)
            {
                char argument[MAX_PATH_LENGTH * 2];
                ExpandShaderArgument(argv[a], argument, sizeof(argument), names, values);

                for (int d = 0; strcmp(argv[a], "{include}") == 0 && d < includeDirectories.Count; d++)
                {
                    AppendArgument(&job->Job.Command, "-I");
                    AppendPathArgument(&job->Job.Command, "", includeDirectories.Items[d]);
                }

                if (strcmp(argv[a], "{include}") != 0)
                {
                    AppendArgument(&job->Job.Command, argument);
                }
            }

            EnsureParentDirectory(job->Output);
            stale[count] = &job->Job;
            count++;
        }

        List_Free(&includeDirectories);
    }

    if (!WriteIfChanged(statePath, &newState))
    {
        fprintf(stderr, "[Shaders] WARNING: Could not write \"%s\"\n", statePath);
    }

    for (int f = 0; shaderCount > 0 && f < formatCount; f++)
    {
        char found[MAX_PATH_LENGTH];
        const char *compiler = formats[f].Count > 0 ? argv[formats[f].First] : "";

        if (!compiler[0] || (strcmp(compiler, "copy") != 0 && !FileExists(compiler) && !SearchPathA(NULL, compiler, ".exe", sizeof(found), found, NULL)))
        {
            fprintf(stderr, "[Shaders] ERROR: The compiler for %s shaders (\"%s\") was not found, so %d shader(s) cannot compile.%s\n", formats[f].Extension, compiler, shaderCount,
                    strcmp(formats[f].Extension, ".dxil") == 0 ? " Install a Windows SDK with the Visual Studio Installer." : "");
            succeeded = false;
        }
    }

    if (succeeded && count + copied == 0)
    {
        printf("[Shaders] Shaders are up to date.\n");
    }
    else if (succeeded)
    {
        succeeded = RunJobs(stale, count, ShaderFinished);
    }

    for (int i = 0; i < count; i++)
    {
        Buffer_Free(&jobs[i].Job.Command);
    }

    free(jobs);
    free(stale);
    Buffer_Free(&signature);
    Buffer_Free(&newState);
    List_Free(&sources);
    return succeeded ? 0 : 1;
}

#pragma endregion

#pragma region Bake
/* Tools Bake: packs the assets into Assets.data, or into a header for EMBED_ASSETS, optionally encrypted */

static bool BakeFolder(const char *directory, bool shaders, const char *headerPath, Buffer *index, List *files, unsigned long long *dataSize)
{
    List found = {};
    bool succeeded = true;
    ListEntries(directory, &found, NULL);
    qsort(found.Items, (size_t)found.Count, sizeof(char *), CompareInsensitiveItems);

    for (int i = 0; i < found.Count; i++)
    {
        const char *path = found.Items[i];
        const bool buildFile = EndsWithExtension(path, ".state") || EndsWithExtension(path, ".log") || EndsWithExtension(path, ".tmp") || _strnicmp(path, headerPath, strlen(headerPath)) == 0;

        const char *fileName = FileName(path);
        const bool systemFile = _stricmp(fileName, "Thumbs.db") == 0 || _stricmp(fileName, "desktop.ini") == 0 || _stricmp(fileName, ".DS_Store") == 0;

        if (_stricmp(fileName, "keep") == 0 || buildFile || systemFile || (!shaders && IsShaderSource(path)))
        {
            continue;
        }

        WIN32_FILE_ATTRIBUTE_DATA info;
        const bool readable = IsPlainName(path) && GetFileAttributesExA(path, GetFileExInfoStandard, &info);

        if (!readable || info.nFileSizeHigh != 0 || info.nFileSizeLow >= 0x80000000u)
        {
            fprintf(stderr, readable ? "[Bake] ERROR: \"%s\" is 2 GB or larger: split it\n" : "[Bake] ERROR: Could not read \"%s\" (a name outside plain ASCII can't be baked: rename it)\n", path);
            succeeded = false;
            continue;
        }

        const char *name = path + strlen(directory) + 1;
        const unsigned int fields[4] = {(unsigned int)(*dataSize & 0xFFFFFFFFu), (unsigned int)(*dataSize >> 32), info.nFileSizeLow, (unsigned int)strlen(name)};
        Buffer_Append(index, (const char *)fields, sizeof(fields));
        Buffer_Append(index, name, fields[3]);
        List_Add(files, path);
        *dataSize += info.nFileSizeLow;
        printf("[Bake] %s\n", name);
    }

    List_Free(&found);
    return succeeded;
}

static unsigned long long Scramble(unsigned long long x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static void Encrypt(char *data, size_t length, unsigned long long position, unsigned long long key)
{
    unsigned long long block = 0;

    for (size_t i = 0; key && i < length; i++, position++)
    {
        block = i == 0 || position % 8 == 0 ? Scramble(key + position / 8) : block;
        data[i] ^= position < 12 ? 0 : (char)(block >> (position % 8 * 8));
    }
}

static bool SameFiles(const char *a, const char *b)
{
    FILE *first = fopen(a, "rb");
    FILE *second = fopen(b, "rb");
    bool same = first && second;
    static char chunkA[1 << 16];
    static char chunkB[1 << 16];

    while (same)
    {
        const size_t readA = fread(chunkA, 1, sizeof(chunkA), first);
        const size_t readB = fread(chunkB, 1, sizeof(chunkB), second);
        same = readA == readB && memcmp(chunkA, chunkB, readA) == 0;

        if (readA == 0)
        {
            break;
        }
    }

    if (first)
    {
        fclose(first);
    }

    if (second)
    {
        fclose(second);
    }

    return same;
}

static int BakeCommand(int argc, char *argv[])
{
    const bool encrypt = argc > 1 && strcmp(argv[argc - 1], "--encrypt") == 0;
    argc -= encrypt ? 1 : 0;

    if (argc < 4)
    {
        fprintf(stderr, "[Bake] Usage: Tools Bake <header> <pack, or \"\" to embed it (EMBED_ASSETS)> <assets folder> [<compiled shaders folder>] [--encrypt]\n");
        return 1;
    }

    char headerPath[MAX_PATH_LENGTH];
    char packPath[MAX_PATH_LENGTH];
    char directory[MAX_PATH_LENGTH];
    char temporaryPath[MAX_PATH_LENGTH];
    snprintf(headerPath, sizeof(headerPath), "%s", argv[1]);
    snprintf(packPath, sizeof(packPath), "%s", argv[2]);
    snprintf(directory, sizeof(directory), "%s", argv[3]);
    NormalizePath(headerPath);
    NormalizePath(packPath);
    NormalizePath(directory);
    snprintf(temporaryPath, sizeof(temporaryPath), "%s.pack.tmp", headerPath);

    Buffer index = {};
    Buffer head = {};
    Buffer header = {};
    List files = {};
    unsigned long long dataSize = 0;
    bool succeeded = BakeFolder(directory, false, headerPath, &index, &files, &dataSize);

    if (argc > 4)
    {
        snprintf(directory, sizeof(directory), "%s", argv[4]);
        NormalizePath(directory);
        succeeded = BakeFolder(directory, true, headerPath, &index, &files, &dataSize) && succeeded;
    }

    unsigned int contentHash = 2166136261u;

    for (int i = 0; succeeded && i < files.Count; i++)
    {
        static unsigned char chunk[1 << 20];
        FILE *in = fopen(files.Items[i], "rb");
        size_t read = 0;
        succeeded = in != NULL;

        while (in && (read = fread(chunk, 1, sizeof(chunk), in)) > 0)
        {
            for (size_t b = 0; b < read; b++)
            {
                contentHash = (contentHash ^ chunk[b]) * 16777619u;
            }
        }

        if (in)
        {
            fclose(in);
        }
    }

    const unsigned int packHeader[3] = {0x4B434150u, contentHash, (unsigned int)index.Length};
    Buffer_Append(&head, (const char *)packHeader, sizeof(packHeader));
    Buffer_Append(&head, index.Data, index.Length);
    unsigned int hash = 2166136261u;
    unsigned long long key = 0;

    for (size_t i = 0; i < head.Length; i++)
    {
        hash = (hash ^ (unsigned char)head.Data[i]) * 16777619u;
        key = encrypt ? Scramble(key ^ (unsigned char)head.Data[i]) | 1 : 0;
    }

    Encrypt(head.Data, head.Length, 0, key);
    EnsureParentDirectory(temporaryPath);
    FILE *out = fopen(temporaryPath, "wb");
    unsigned long long position = head.Length;
    succeeded = out && fwrite(head.Data, 1, head.Length, out) == head.Length && succeeded;

    for (int i = 0; out && succeeded && i < files.Count; i++)
    {
        static char chunk[1 << 20];
        FILE *in = fopen(files.Items[i], "rb");
        size_t read = 0;

        while (in && (read = fread(chunk, 1, sizeof(chunk), in)) > 0)
        {
            Encrypt(chunk, read, position, key);
            succeeded = fwrite(chunk, 1, read, out) == read && succeeded;
            position += read;
        }

        if (in)
        {
            fclose(in);
        }
    }

    if (out)
    {
        fclose(out);
    }

    if (succeeded && position != head.Length + dataSize)
    {
        fprintf(stderr, "[Bake] ERROR: An asset changed while it was being baked. Build again.\n");
        succeeded = false;
    }

    /* EMBED_ASSETS: the header #embeds the pack (clang 19 or later) */
    char line[MAX_PATH_LENGTH + 128];
    char embedPath[MAX_PATH_LENGTH];
    snprintf(embedPath, sizeof(embedPath), "%.*s.pack", (int)(strlen(headerPath) - strlen(strrchr(headerPath, '.') ? strrchr(headerPath, '.') : "")), headerPath);
    Buffer_AppendText(&header, "#pragma once\r\n\r\n");

    if (packPath[0])
    {
        Buffer_AppendText(&header, "extern const unsigned char BakedPack[] = {0};\r\n");
    }
    else
    {
        snprintf(line, sizeof(line), "#pragma clang diagnostic push\r\n#pragma clang diagnostic ignored \"-Wc23-extensions\"\r\nextern const unsigned char BakedPack[] = {\r\n#embed \"%s\"\r\n};\r\n#pragma clang diagnostic pop\r\n", FileName(embedPath));
        Buffer_AppendText(&header, line);
    }

    snprintf(line, sizeof(line), "extern const unsigned int BakedPackSize = %u;\r\nextern const char BakedPackFile[] = \"%s\";\r\nextern const unsigned int BakedPackHash = %uu;\r\nextern const unsigned long long BakedPackKey = %lluull;\r\n",
             packPath[0] ? 0u : (unsigned int)position, packPath[0] ? FileName(packPath) : "", hash, key);
    Buffer_AppendText(&header, line);

    if (succeeded && !packPath[0] && !SameFiles(temporaryPath, embedPath) && !MoveFileExA(temporaryPath, embedPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
    {
        fprintf(stderr, "[Bake] ERROR: Could not write \"%s\"\n", embedPath);
        succeeded = false;
    }

    if (packPath[0])
    {
        EnsureParentDirectory(packPath);
    }

    if (succeeded && packPath[0] && !SameFiles(temporaryPath, packPath) && !MoveFileExA(temporaryPath, packPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
    {
        fprintf(stderr, "[Bake] ERROR: Could not write \"%s\"\n", packPath);
        succeeded = false;
    }

    if (succeeded && !WriteIfChanged(headerPath, &header))
    {
        fprintf(stderr, "[Bake] ERROR: Could not write \"%s\"\n", headerPath);
        succeeded = false;
    }

    DeleteFileA(temporaryPath);
    Buffer_Free(&index);
    Buffer_Free(&head);
    Buffer_Free(&header);
    List_Free(&files);
    return succeeded ? 0 : 1;
}

#pragma endregion

#pragma region Symbols
/* Tools Symbols keeps each Release exe with its .pdb, to read its crash reports */

static unsigned int Read16(const Buffer &file, size_t at)
{
    const unsigned char *data = (const unsigned char *)file.Data;
    return at + 2 <= file.Length ? (unsigned int)(data[at] | data[at + 1] << 8) : 0u;
}

static unsigned int Read32(const Buffer &file, size_t at)
{
    return Read16(file, at + 2) << 16 | (at + 4 <= file.Length ? Read16(file, at) : 0u);
}

static bool CopyIntoStore(const char *source, const char *store, const char *key)
{
    char target[MAX_PATH_LENGTH];
    snprintf(target, sizeof(target), "%s/%s/%s/%s", store, FileName(source), key, FileName(source));
    EnsureParentDirectory(target);
    return CopyFileA(source, target, FALSE) != 0;
}

static bool ReadExeIdentity(const char *exe, char *exeKey, char *pdbKey, char *pdbName)
{
    Buffer image = {};
    exeKey[0] = pdbKey[0] = pdbName[0] = '\0';

    if (!ReadWholeFile(exe, &image) || !image.Data)
    {
        return false;
    }

    const unsigned char *data = (const unsigned char *)image.Data;
    const size_t length = image.Length;
    auto read16 = [&](size_t at) { return Read16(image, at); };
    auto read32 = [&](size_t at) { return Read32(image, at); };

    const size_t header = read32(0x3C);
    const bool valid = length > 0x40 && read16(0) == 0x5A4D && read32(header) == 0x00004550;
    const size_t optional = header + 24;
    const size_t directories = optional + (read16(optional) == 0x20B ? 112 : 96);
    const unsigned int debugAddress = valid ? read32(directories + 6 * 8) : 0;
    const unsigned int debugSize = valid ? read32(directories + 6 * 8 + 4) : 0;
    const size_t sections = optional + read16(header + 20);
    const unsigned int sectionCount = valid ? read16(header + 6) : 0;
    size_t debugOffset = 0;

    if (valid)
    {
        snprintf(exeKey, 64, "%08X%x", read32(header + 8), read32(optional + 56));
    }

    for (unsigned int i = 0; i < sectionCount && debugAddress; i++)
    {
        const size_t section = sections + i * 40;
        const unsigned int address = read32(section + 12);
        const unsigned int size = read32(section + 8) > read32(section + 16) ? read32(section + 8) : read32(section + 16);

        if (debugAddress >= address && debugAddress < address + size)
        {
            debugOffset = read32(section + 20) + (debugAddress - address);
        }
    }

    for (unsigned int entry = 0; debugOffset && entry < debugSize / 28; entry++)
    {
        const size_t at = debugOffset + entry * 28;
        const size_t record = read32(at + 24);

        if (read32(at + 12) == 2 && read32(record) == 0x53445352 && record + 24 < length)
        {
            const unsigned char *guid = data + record + 4;
            snprintf(pdbKey, 64, "%08X%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X%X", read32(record + 4), read16(record + 8), read16(record + 10),
                     guid[8], guid[9], guid[10], guid[11], guid[12], guid[13], guid[14], guid[15], read32(record + 20));
            snprintf(pdbName, MAX_PATH_LENGTH, "%s", (const char *)data + record + 24);
            ReplaceChar(pdbName, '\\', '/');
            memmove(pdbName, FileName(pdbName), strlen(FileName(pdbName)) + 1);
        }
    }

    Buffer_Free(&image);
    return valid && pdbKey[0] && pdbName[0];
}

static int SymbolsCommand(int argc, char *argv[])
{
    if (argc < 4)
    {
        fprintf(stderr, "[Symbols] Usage: Tools Symbols <exe> <pdb> <store folder>\n");
        return 1;
    }

    char exe[MAX_PATH_LENGTH];
    char pdb[MAX_PATH_LENGTH];
    char store[MAX_PATH_LENGTH];
    char exeKey[64];
    char pdbKey[64];
    char pdbName[MAX_PATH_LENGTH];
    snprintf(exe, sizeof(exe), "%s", argv[1]);
    snprintf(pdb, sizeof(pdb), "%s", argv[2]);
    snprintf(store, sizeof(store), "%s", argv[3]);
    NormalizePath(exe);
    NormalizePath(pdb);
    NormalizePath(store);

    if (!ReadExeIdentity(exe, exeKey, pdbKey, pdbName) || !FileExists(pdb))
    {
        fprintf(stderr, "[Symbols] WARNING: %s has no matching debug symbols, so its crash reports can't be read. (Compressing with UPX removes them.)\n", FileName(exe));
        return 0;
    }

    if (!CopyIntoStore(exe, store, exeKey) || !CopyIntoStore(pdb, store, pdbKey))
    {
        fprintf(stderr, "[Symbols] ERROR: Could not copy %s and its .pdb into \"%s\"\n", FileName(exe), store);
        return 1;
    }

    printf("[Symbols] Kept %s and its .pdb for its crash reports, in %s\n", FileName(exe), store);
    return 0;
}

#pragma endregion

#pragma region Icon
/* Tools Icon: Icon.png to every icon size and format, keeping pixel art sharp */

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

/* 32-bit BGRA pixels, alpha not premultiplied */
typedef struct
{
    UINT Width;
    UINT Height;
    BYTE *Pixels;
} Image;

static IWICImagingFactory *ImagingFactory(void)
{
    static IWICImagingFactory *factory = NULL;

    if (!factory)
    {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void **)&factory);
    }

    return factory;
}

static bool Image_Load(const char *path, Image *image)
{
    IWICImagingFactory *factory = ImagingFactory();
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    wchar_t wide[MAX_PATH_LENGTH];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, MAX_PATH_LENGTH);
    *image = Image{};

    bool loaded = factory && SUCCEEDED(factory->CreateDecoderFromFilename(wide, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
                  SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                  SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom)) &&
                  SUCCEEDED(converter->GetSize(&image->Width, &image->Height)) && image->Width > 0 && image->Height > 0;

    if (loaded)
    {
        image->Pixels = (BYTE *)malloc((size_t)image->Width * image->Height * 4);
        loaded = image->Pixels && SUCCEEDED(converter->CopyPixels(NULL, image->Width * 4, image->Width * image->Height * 4, image->Pixels));
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    return loaded;
}

/* source centered on a transparent size x size square, scaled to fit within content x content. Enlarging goes by whole
   pixels when that fills most of the space (pixel art stays sharp), otherwise by the exact ratio; shrinking averages */
static void Image_Fit(const Image *source, UINT size, double content, Image *result)
{
    result->Width = size;
    result->Height = size;
    result->Pixels = (BYTE *)calloc((size_t)size * size, 4);

    if (!result->Pixels)
    {
        OutOfMemory();
    }

    const double ratio = content / (double)(source->Width > source->Height ? source->Width : source->Height);
    double scale = (double)(UINT)ratio;
    scale = scale < 1.0 || scale / ratio < 0.8 ? ratio : scale;
    const UINT width = (UINT)(source->Width * scale + 0.5) < size ? (UINT)(source->Width * scale + 0.5) : size;
    const UINT height = (UINT)(source->Height * scale + 0.5) < size ? (UINT)(source->Height * scale + 0.5) : size;
    const UINT left = (size - width) / 2;
    const UINT top = (size - height) / 2;

    for (UINT y = 0; y < height; y++)
    {
        for (UINT x = 0; x < width; x++)
        {
            BYTE *target = result->Pixels + ((size_t)(top + y) * size + left + x) * 4;
            UINT x0 = (UINT)(x / scale), y0 = (UINT)(y / scale);
            UINT x1 = scale >= 1.0 ? x0 + 1 : (UINT)((x + 1) / scale + 0.999), y1 = scale >= 1.0 ? y0 + 1 : (UINT)((y + 1) / scale + 0.999);
            double sum[4] = {};
            double count = 0;

            for (UINT sy = y0; sy < y1 && sy < source->Height; sy++)
            {
                for (UINT sx = x0; sx < x1 && sx < source->Width; sx++)
                {
                    const BYTE *pixel = source->Pixels + ((size_t)sy * source->Width + sx) * 4;
                    const double alpha = pixel[3] / 255.0;
                    sum[0] += pixel[0] * alpha;
                    sum[1] += pixel[1] * alpha;
                    sum[2] += pixel[2] * alpha;
                    sum[3] += alpha;
                    count++;
                }
            }

            for (int c = 0; count > 0 && sum[3] > 0 && c < 3; c++)
            {
                target[c] = (BYTE)(sum[c] / sum[3] + 0.5);
            }

            target[3] = count > 0 ? (BYTE)(sum[3] / count * 255.0 + 0.5) : 0;
        }
    }
}

static bool Image_EncodePng(const Image *image, Buffer *png)
{
    IWICImagingFactory *factory = ImagingFactory();
    IStream *stream = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;

    bool encoded = factory && SUCCEEDED(CreateStreamOnHGlobal(NULL, TRUE, &stream)) && SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, NULL, &encoder)) &&
                   SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) && SUCCEEDED(encoder->CreateNewFrame(&frame, NULL)) &&
                   SUCCEEDED(frame->Initialize(NULL)) && SUCCEEDED(frame->SetSize(image->Width, image->Height)) &&
                   SUCCEEDED(frame->SetPixelFormat(&format)) && IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA) &&
                   SUCCEEDED(frame->WritePixels(image->Height, image->Width * 4, image->Width * image->Height * 4, image->Pixels)) &&
                   SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());

    LARGE_INTEGER zero = {};
    ULARGE_INTEGER length = {};
    HGLOBAL global = NULL;
    encoded = encoded && SUCCEEDED(stream->Seek(zero, STREAM_SEEK_CUR, &length)) && SUCCEEDED(GetHGlobalFromStream(stream, &global));

    if (encoded)
    {
        Buffer_Append(png, (const char *)GlobalLock(global), (size_t)length.QuadPart);
        GlobalUnlock(global);
    }

    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    return encoded;
}

/* Tools Icon <image> <output.png> <size> [<content size>]: the image fitted on a size x size square
   Tools Icon <image> <output.ico>: a Windows icon with every size from 16 to 256
   Tools Icon <image> --color: the first solid color from the top middle of the image, as #RRGGBB */
static int IconCommand(int argc, char *argv[])
{
    Image source = {};

    if (argc < 3)
    {
        fprintf(stderr, "[Icon] Usage: Tools Icon <image> <output.png> <size> [<content size>] | <output.ico> | --color\n");
        return 1;
    }

    if (!Image_Load(argv[1], &source))
    {
        fprintf(stderr, "[Icon] ERROR: Could not read \"%s\" as an image.\n", argv[1]);
        return 1;
    }

    if (strcmp(argv[2], "--color") == 0)
    {
        BYTE *pixel = NULL;

        for (UINT y = 0; y < source.Height && !pixel; y++)
        {
            BYTE *candidate = source.Pixels + ((size_t)y * source.Width + source.Width / 2) * 4;
            pixel = candidate[3] >= 250 ? candidate : NULL;
        }

        printf("#%02X%02X%02X\n", pixel ? pixel[2] : 255, pixel ? pixel[1] : 255, pixel ? pixel[0] : 255);
        free(source.Pixels);
        return 0;
    }

    char output[MAX_PATH_LENGTH];
    snprintf(output, sizeof(output), "%s", argv[2]);
    NormalizePath(output);
    EnsureParentDirectory(output);
    Buffer file = {};
    bool written = true;

    if (EndsWithExtension(output, ".ico"))
    {
        static const UINT sizes[] = {16, 24, 32, 48, 64, 128, 256};
        const int count = (int)(sizeof(sizes) / sizeof(sizes[0]));
        Buffer images[sizeof(sizes) / sizeof(sizes[0])] = {};
        unsigned int offset = 6 + 16 * count;
        const unsigned short header[3] = {0, 1, (unsigned short)count};
        Buffer_Append(&file, (const char *)header, sizeof(header));

        for (int i = 0; i < count && written; i++)
        {
            Image fitted = {};
            Image_Fit(&source, sizes[i], sizes[i], &fitted);
            written = Image_EncodePng(&fitted, &images[i]);
            free(fitted.Pixels);
            const unsigned char entry[8] = {(unsigned char)sizes[i], (unsigned char)sizes[i], 0, 0, 1, 0, 32, 0};
            const unsigned int location[2] = {(unsigned int)images[i].Length, offset};
            Buffer_Append(&file, (const char *)entry, sizeof(entry));
            Buffer_Append(&file, (const char *)location, sizeof(location));
            offset += (unsigned int)images[i].Length;
        }

        for (int i = 0; i < count; i++)
        {
            Buffer_Append(&file, images[i].Data ? images[i].Data : "", images[i].Length);
            Buffer_Free(&images[i]);
        }
    }
    else
    {
        const UINT size = argc > 3 ? (UINT)atoi(argv[3]) : source.Width;
        const double content = argc > 4 ? atof(argv[4]) : size;
        Image fitted = {};
        Image_Fit(&source, size > 0 ? size : 1, content > 0 ? content : 1, &fitted);
        written = Image_EncodePng(&fitted, &file);
        free(fitted.Pixels);
    }

    written = written && WriteIfChanged(output, &file);
    free(source.Pixels);
    Buffer_Free(&file);

    if (!written)
    {
        fprintf(stderr, "[Icon] ERROR: Could not write \"%s\".\n", output);
    }

    return written ? 0 : 1;
}

#pragma endregion

#pragma region Watch
/* Tools Watch: while Visual Studio is open, refreshes the generated headers and the project's PROGRAM_NAME */

static bool IsProcessRunning(DWORD id, const char *name)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    bool found = false;

    for (bool more = Process32First(snapshot, &entry); more && !found; more = Process32Next(snapshot, &entry))
    {
        found = id ? entry.th32ProcessID == id : _stricmp(entry.szExeFile, name) == 0;
    }

    CloseHandle(snapshot);
    return found;
}

static DWORD FindVisualStudio(void)
{
    DWORD id = GetCurrentProcessId();

    for (int depth = 0; depth < 32 && id; depth++)
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 entry = {};
        entry.dwSize = sizeof(entry);
        DWORD parent = 0;

        for (bool more = Process32First(snapshot, &entry); more; more = Process32Next(snapshot, &entry))
        {
            if (entry.th32ProcessID == id)
            {
                if (_stricmp(entry.szExeFile, "devenv.exe") == 0)
                {
                    CloseHandle(snapshot);
                    return id;
                }

                parent = entry.th32ParentProcessID;
                break;
            }
        }

        CloseHandle(snapshot);
        id = parent;
    }

    return 0;
}

/* Tools.exe again, with arguments: waits for it, or leaves it running detached */
static void RunSelf(const char *const *arguments, int count, bool detached)
{
    char exe[MAX_PATH_LENGTH];
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    Buffer command = {};
    AppendArgument(&command, exe);

    for (int i = 0; i < count; i++)
    {
        AppendArgument(&command, arguments[i]);
    }

    STARTUPINFOA startup = {};
    PROCESS_INFORMATION process = {};
    startup.cb = sizeof(startup);

    if (CreateProcessA(NULL, command.Data, NULL, NULL, FALSE, detached ? DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP : CREATE_NO_WINDOW, NULL, NULL, &startup, &process))
    {
        if (!detached)
        {
            WaitForSingleObject(process.hProcess, INFINITE);
        }

        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
    }

    Buffer_Free(&command);
}

static void SyncProgramName(const char *root)
{
    Buffer name = {};
    List projects = {};

    if (ConfigValue(root, "PROGRAM_NAME", &name))
    {
        ListMatches(root, "*.vcxproj", &projects);
    }

    for (int i = 0; i < projects.Count; i++)
    {
        if (!EndsWithExtension(projects.Items[i], ".vcxproj"))
        {
            continue;
        }

        Buffer project = {};
        ReadWholeFile(projects.Items[i], &project);
        char *open = project.Data ? strstr(project.Data, "<GameName>") : NULL;
        char *close = open ? strstr(open, "</GameName>") : NULL;

        if (close)
        {
            Buffer updated = {};
            Buffer_Append(&updated, project.Data, (size_t)(open - project.Data) + 10);
            Buffer_Append(&updated, name.Data, name.Length);
            Buffer_AppendText(&updated, close);
            WriteIfChanged(projects.Items[i], &updated);
            Buffer_Free(&updated);
        }

        Buffer_Free(&project);
    }

    List_Free(&projects);
    Buffer_Free(&name);
}

static int WatchCommand(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "[Watch] Usage: Tools Watch <project root>\n");
        return 1;
    }

    char root[MAX_PATH_LENGTH];
    snprintf(root, sizeof(root), "%s", argv[1]);
    NormalizePath(root);

    if (argc < 4 || strcmp(argv[2], "--detached") != 0)
    {
        char visualStudio[32];
        snprintf(visualStudio, sizeof(visualStudio), "%lu", (unsigned long)FindVisualStudio());
        const char *const arguments[] = {"Watch", root, "--detached", visualStudio};
        RunSelf(arguments, 4, true);
        return 0;
    }

    char mutexName[MAX_PATH_LENGTH];
    snprintf(mutexName, sizeof(mutexName), "Local\\FrameworkWatcher%08X", HashPath(root));
    CreateMutexA(NULL, TRUE, mutexName);

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        return 0;
    }

    const DWORD visualStudio = (DWORD)strtoul(argv[3], NULL, 10);
    char codeDirectory[MAX_PATH_LENGTH];
    char configPath[MAX_PATH_LENGTH];
    char temporary[MAX_PATH_LENGTH];
    const char *const includes[] = {"Includes", root};
    snprintf(codeDirectory, sizeof(codeDirectory), "%s/Program", root);
    snprintf(configPath, sizeof(configPath), "%s/Config.bat", root);

    GetTempPathA(sizeof(temporary), temporary);
    SetCurrentDirectoryA(temporary);

    HANDLE change = FindFirstChangeNotificationA(codeDirectory, TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE);
    ULONGLONG configTime = 0;
    ULONGLONG toolsTime = 0;
    ULONGLONG currentToolsTime = 0;
    char toolsPath[MAX_PATH_LENGTH];
    snprintf(toolsPath, sizeof(toolsPath), "%s/Builds/Cache/Tools.exe", root);
    GetWriteTime(toolsPath, &toolsTime);

    while (change != INVALID_HANDLE_VALUE && DirectoryExists(codeDirectory) && IsProcessRunning(visualStudio, "devenv.exe") && GetWriteTime(toolsPath, &currentToolsTime) && currentToolsTime == toolsTime)
    {
        ULONGLONG time = 0;

        if (GetWriteTime(configPath, &time) && time != configTime)
        {
            configTime = time;
            SyncProgramName(root);
            RunSelf(includes, 2, false);
        }

        if (WaitForSingleObject(change, 500) == WAIT_OBJECT_0)
        {
            Sleep(300);
            FindNextChangeNotification(change);
            RunSelf(includes, 2, false);
        }
    }

    FindCloseChangeNotification(change);
    return 0;
}

#pragma endregion

/* Tools Version <Config.bat> <NAME> <installed>: records installed as NAME when it is blank; exit code 0 when installed
   is older than NAME, so the build runs its setup */
static int VersionCommand(int argc, char *argv[])
{
    if (argc < 4)
    {
        fprintf(stderr, "[Version] Usage: Tools Version <Config.bat> <NAME> <installed version>\n");
        return 2;
    }

    Buffer text = {};
    char key[256];
    snprintf(key, sizeof(key), "set \"%s=", argv[2]);
    const char *found = ReadWholeFile(argv[1], &text) && text.Data ? strstr(text.Data, key) : NULL;
    const char *value = found ? found + strlen(key) : NULL;
    const char *end = value ? strchr(value, '"') : NULL;

    if (!end)
    {
        Buffer_Free(&text);
        return 1;
    }

    if (end == value && argv[3][0])
    {
        Buffer recorded = {};
        Buffer_Append(&recorded, text.Data, (size_t)(value - text.Data));
        Buffer_AppendText(&recorded, argv[3]);
        Buffer_AppendText(&recorded, end);
        WriteIfChanged(argv[1], &recorded);
        printf("[Build] %s=%s recorded in the platform's Config.bat: the least version this project builds with.\n", argv[2], argv[3]);
        Buffer_Free(&recorded);
        Buffer_Free(&text);
        return 1;
    }

    const char *installed = argv[3] + strcspn(argv[3], "0123456789");
    const char *least = value;
    int older = 1;

    for (int part = 0; part < 4 && older == 1; part++)
    {
        char *stop = NULL;
        const unsigned long a = strtoul(installed, &stop, 10);
        installed = *stop == '.' ? stop + 1 : "";
        const unsigned long b = strtoul(least, &stop, 10);
        least = *stop == '.' ? stop + 1 : "";
        older = a < b ? 0 : a > b ? 2 : 1;
    }

    Buffer_Free(&text);
    return older == 0 ? 0 : 1;
}

int main(int argc, char *argv[])
{
    static const struct { const char *Name; int (*Run)(int argc, char *argv[]); } commands[] = {
        {"Includes", IncludesCommand},
        {"Compile", CompileCommand},
        {"Shaders", ShadersCommand},
        {"Bake", BakeCommand},
        {"Watch", WatchCommand},
        {"Symbols", SymbolsCommand},
        {"Icon", IconCommand},
        {"Version", VersionCommand},
    };

    for (size_t i = 0; argc >= 2 && i < sizeof(commands) / sizeof(commands[0]); i++)
    {
        if (_stricmp(argv[1], commands[i].Name) == 0)
        {
            return commands[i].Run(argc - 1, argv + 1);
        }
    }

    fprintf(stderr, "[Tools] Usage: Tools Includes|Compile|Shaders|Bake|Watch|Symbols|Icon|Version ...\n");
    return 1;
}
