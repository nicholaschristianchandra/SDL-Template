#pragma region Boilerplates

#if defined(_WIN32) && !defined(_GAMING_XBOX) && !defined(FRAMEWORK_NO_CRASH_REPORTS)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <dbghelp.h>
    #include <signal.h>
    #include <stdlib.h>
    #pragma comment(lib, "user32.lib")
#endif

#define SDL_MAIN_USE_CALLBACKS
#define FRAMEWORK_HOST
#define FRAMEWORK_UNIT_DECLARATIONS
#include <Framework/Framework.hpp>
#include <SDL3/SDL_main.h>
#include <new>
#ifndef IS_DEBUG
    #if __has_include(<BakedAssets.hpp>)
        #include <BakedAssets.hpp>
    #else
        extern const unsigned char BakedPack[] = { 0 };
        extern const unsigned int BakedPackSize = 0;
        extern const char BakedPackFile[] = "";
        extern const unsigned int BakedPackHash = 0;
        extern const unsigned long long BakedPackKey = 0;
    #endif

    namespace Program::Lifecycle
    {
        extern "C" SDL_AppResult Startup(void **appState, int argc, char *argv[]);
        extern "C" SDL_AppResult Update(void *appState);
        extern "C" SDL_AppResult Event(void *appState, SDL_Event *event);
        extern "C" void Shutdown(void *appState, SDL_AppResult result);
    }
#endif

static Program::State *GameState;

#if defined(_WIN32) && !defined(_GAMING_XBOX) && !defined(FRAMEWORK_NO_CRASH_REPORTS)

static char CrashFolder[MAX_PATH];
static DWORD CrashThread;
static EXCEPTION_POINTERS *CrashException;
static HANDLE CrashEvent;
static HANDLE CrashWriter;

static DWORD WINAPI WriteCrashReportThread(void *)
{
    typedef BOOL(WINAPI * WriteDumpFunction)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
    HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
    WriteDumpFunction writeDump = dbghelp ? (WriteDumpFunction)(void *)GetProcAddress(dbghelp, "MiniDumpWriteDump") : nullptr;

    WaitForSingleObject(CrashEvent, INFINITE);
    EXCEPTION_POINTERS *exception = CrashException;

    SYSTEMTIME time;
    GetLocalTime(&time);
    char path[MAX_PATH + 64];
    snprintf(path, sizeof(path), "%s\\%s_%02u-%02u-%04u_%02u-%02u-%02u.dmp", CrashFolder, PROGRAM_VERSION, time.wDay, time.wMonth, time.wYear, time.wHour, time.wMinute, time.wSecond);

    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    BOOL written = FALSE;

    if (writeDump && file != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION information = {CrashThread, exception, FALSE};
        written = writeDump(GetCurrentProcess(), GetCurrentProcessId(), file, (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory), &information, NULL, NULL);
    }

    if (file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file);
    }

    char message[MAX_PATH + 256];
    snprintf(message, sizeof(message), written ? "%s crashed. A crash report was saved to:\n%s" : "%s crashed.", PROGRAM_NAME, path);
    MessageBoxA(NULL, message, PROGRAM_NAME, MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);

    return 0;
}

static LONG WINAPI WriteCrashReport(EXCEPTION_POINTERS *exception)
{
    CrashThread = GetCurrentThreadId();
    CrashException = exception;
    SetEvent(CrashEvent);
    WaitForSingleObject(CrashWriter, INFINITE);

    return EXCEPTION_EXECUTE_HANDLER;
}

static void __cdecl RaiseCrash(int)
{
    RaiseException(0xE0000001, EXCEPTION_NONCONTINUABLE, 0, NULL);
}

static void __cdecl RaiseCrashOnInvalidParameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t)
{
    RaiseCrash(0);
}

static void InstallCrashReports(void)
{
    char appData[MAX_PATH];

    if (GetEnvironmentVariableA("LOCALAPPDATA", appData, sizeof(appData)))
    {
        snprintf(CrashFolder, sizeof(CrashFolder), "%s\\%s", appData, PROGRAM_NAME);
        CreateDirectoryA(CrashFolder, NULL);
        strncat(CrashFolder, "\\Crashes", sizeof(CrashFolder) - strlen(CrashFolder) - 1);
        CreateDirectoryA(CrashFolder, NULL);
        CrashEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        CrashWriter = CreateThread(NULL, 0, WriteCrashReportThread, NULL, 0, NULL);
        SetUnhandledExceptionFilter(WriteCrashReport);
        signal(SIGABRT, RaiseCrash);
        _set_invalid_parameter_handler(RaiseCrashOnInvalidParameter);
        ULONG guarantee = 32 * 1024;
        SetThreadStackGuarantee(&guarantee);
    }
}

#else

static void InstallCrashReports(void)
{
}

#endif

#ifdef IS_DEBUG

static SDL_AppInit_func StartupFunction;
static SDL_AppIterate_func UpdateFunction;
static SDL_AppEvent_func EventFunction;
static SDL_AppQuit_func ShutdownFunction;

static String(1024) DllPath;
static String(1024) CopyPaths[2];
static int LoadedSlot = 1;
static SDL_SharedObject *Dll;
static SDL_Time DllTime;

static void SendReloadEvent(void *appState, Uint32 type)
{
    SDL_Event event = {};
    event.type = type;
    event.user.timestamp = SDL_GetTicksNS();
    EventFunction(appState, &event);
}

static bool IsStateCompatible(SDL_SharedObject *dll)
{
    const unsigned int *size = (const unsigned int *)(void *)SDL_LoadFunction(dll, "ProgramStateSize");
    const unsigned int *signature = (const unsigned int *)(void *)SDL_LoadFunction(dll, "ProgramStateSignature");

    return size && *size == sizeof(Program::State) && signature && *signature == FRAMEWORK_STATE_SIGNATURE;
}

static bool LoadProgram(void *appState)
{
    SDL_PathInfo info;

    if (!SDL_GetPathInfo(DllPath, &info) || info.modify_time == DllTime)
    {
        if (!Dll)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not find %s. Is it built?", DllPath);
        }

        return Dll != nullptr;
    }

    DllTime = info.modify_time;
    const int slot = 1 - LoadedSlot;
    SDL_SharedObject *next = SDL_CopyFile(DllPath, CopyPaths[slot]) ? SDL_LoadObject(CopyPaths[slot]) : nullptr;

    if (!next || !IsStateCompatible(next))
    {
        if (next)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Program.dll was not loaded: a Types file or Utilities.hpp changed since the game started, so the running game's state no longer fits the new code. Close the game and start it again.");
            SDL_UnloadObject(next);
        }
        else
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load %s: %s", DllPath, SDL_GetError());
        }

        SDL_RemovePath(CopyPaths[slot]);

        return Dll != nullptr;
    }

    /* Checked before unloading the old one, which keeps running if this fails */
    if (!SDL_LoadFunction(next, "Startup") || !SDL_LoadFunction(next, "Update") || !SDL_LoadFunction(next, "Event") || !SDL_LoadFunction(next, "Shutdown"))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Program.dll was not loaded: it is missing Startup, Update, Event or Shutdown.");
        SDL_UnloadObject(next);
        SDL_RemovePath(CopyPaths[slot]);

        return Dll != nullptr;
    }

    if (Dll)
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Program.dll changed. Reloading...");
        SendReloadEvent(appState, FRAMEWORK_EVENT_UNLOAD);
        SDL_UnloadObject(Dll);
        SDL_RemovePath(CopyPaths[LoadedSlot]);
    }

    const bool reloading = Dll != nullptr;
    Dll = next;
    LoadedSlot = slot;
    StartupFunction = (SDL_AppInit_func)SDL_LoadFunction(Dll, "Startup");
    UpdateFunction = (SDL_AppIterate_func)SDL_LoadFunction(Dll, "Update");
    EventFunction = (SDL_AppEvent_func)SDL_LoadFunction(Dll, "Event");
    ShutdownFunction = (SDL_AppQuit_func)SDL_LoadFunction(Dll, "Shutdown");

    if (reloading)
    {
        SendReloadEvent(appState, FRAMEWORK_EVENT_RELOAD);
    }

    return true;
}

#else

#define StartupFunction Program::Lifecycle::Startup
#define UpdateFunction Program::Lifecycle::Update
#define EventFunction Program::Lifecycle::Event
#define ShutdownFunction Program::Lifecycle::Shutdown

#endif

#pragma endregion

SDL_AppResult SDL_AppInit(void **appState, int argc, char *argv[])
{
    InstallCrashReports();

    if (SDL_RegisterEvents(FRAMEWORK_EVENT_COUNT) != FRAMEWORK_EVENT_UNLOAD)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not reserve the framework's events.");

        return SDL_APP_FAILURE;
    }

    GameState = (Program::State *)SDL_calloc(1, sizeof(Program::State));

    if (!GameState)
    {
        return SDL_APP_FAILURE;
    }

    new (GameState) Program::State();
    *appState = GameState;

#ifdef IS_DEBUG
    const char *base = SDL_GetBasePath();
    SDL_snprintf(DllPath, sizeof(DllPath), "%sProgram.dll", base);
    SDL_snprintf(CopyPaths[0], sizeof(CopyPaths[0]), "%sProgram_Loaded0.dll", base);
    SDL_snprintf(CopyPaths[1], sizeof(CopyPaths[1]), "%sProgram_Loaded1.dll", base);

    if (!LoadProgram(appState))
    {
        return SDL_APP_FAILURE;
    }
#endif

    return StartupFunction(appState, argc, argv);
}

SDL_AppResult SDL_AppIterate(void *appState)
{
#ifdef IS_DEBUG
    if (!LoadProgram(appState))
    {
        return SDL_APP_FAILURE;
    }
#endif

    return UpdateFunction(appState);
}

SDL_AppResult SDL_AppEvent(void *appState, SDL_Event *event)
{
#ifdef IS_DEBUG
    if (!EventFunction)
    {
        return SDL_APP_CONTINUE;
    }
#endif

    return EventFunction(appState, event);
}

void SDL_AppQuit(void *appState, SDL_AppResult result)
{
#ifdef IS_DEBUG
    if (ShutdownFunction)
    {
        ShutdownFunction(appState, result);
    }

    if (Dll)
    {
        SDL_UnloadObject(Dll);
        SDL_RemovePath(CopyPaths[LoadedSlot]);
        Dll = nullptr;
    }
#else
    if (GameState)
    {
        ShutdownFunction(appState, result);
    }
#endif

    if (GameState)
    {
        GameState->~State();
    }

    SDL_free(GameState);
    GameState = nullptr;
}
