#pragma once

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

inline constexpr decltype(nullptr) null = nullptr;

typedef int validity;
inline constexpr int invalid = -1;

#if !defined(IS_DEBUG)
	#define DllFunction extern "C"
#elif defined(_WIN32)
	#define DllFunction extern "C" __declspec(dllexport)
#else
	#define DllFunction extern "C" __attribute__((visibility("default")))
#endif

template<typename T, size_t Length, bool Sized> struct Utilities_ArrayOf { typedef T Type[Length]; };
template<typename T, size_t Length> struct Utilities_ArrayOf<T, Length, false> { typedef T Type[]; };
template<typename T, size_t Length, bool Sized> using Utilities_Array = typename Utilities_ArrayOf<T, Length, Sized>::Type;

#define STRING_FORMAT_CAPACITY 2048

#if defined(__GNUC__) || defined(__clang__)
	#define String_PrintfFormat(formatIndex, argsIndex)	__attribute__((format(printf, formatIndex, argsIndex)))
#else
	#define String_PrintfFormat(formatIndex, argsIndex)
#endif

// String(64) Name; is a char array that holds up to 64 characters. Use it anywhere a const char * works
// (printf, SDL_RenderDebugText). String:: functions never write past its end: when text doesn't fit, it is cut, and
// the function returns false. Functions returning const char * give one of 8 shared buffers: use the
// result right away, or String::Set it to keep it.
//   String(64) Name;
//   String::Set(Name, "Hero");
//   SDL_RenderDebugText(renderer, 10, 10, String::Format("%s: %d", Name, score));
#define String(...)	Utilities_Array<char, (__VA_ARGS__ + 0) + 1, (sizeof(#__VA_ARGS__) > 1)>

namespace String
{
	namespace Internal
	{
		inline bool Write(char *text, size_t capacity, size_t offset, const char *newText)
		{
			if (capacity == 0) return false;
			if (!newText) newText = "";
			if (offset >= capacity) offset = capacity - 1;
			size_t length = strlen(newText);
			size_t room = capacity - 1 - offset;
			size_t count = length < room ? length : room;
			memmove(text + offset, newText, count);
			text[offset + count] = '\0';
			return length <= room;
		}

		inline size_t BoundedLength(const char *text, size_t capacity)
		{
			size_t length = 0;
			while (length < capacity - 1 && text[length] != '\0') length++;
			return length;
		}

		inline bool ExpandIn(char *text, size_t capacity, const char *newText)
		{
			if (capacity == 0) return false;
			if (!newText) newText = "";
			size_t length = BoundedLength(text, capacity);
			size_t room = capacity - 1 - length;
			size_t newTextLength = strlen(newText);
			size_t count = newTextLength < room ? newTextLength : room;
			memmove(text + length, newText, count);
			text[length + count] = '\0';
			return newTextLength <= room;
		}

		inline bool InsertIn(char *text, size_t capacity, int index, const char *newText)
		{
			if (capacity == 0) return false;
			if (!newText) newText = "";
			size_t length = BoundedLength(text, capacity);

			size_t position = index < 0 ? 0 : (size_t)index;
			if (position > length) position = length;

			size_t room = capacity - 1 - length;
			size_t newTextLength = strlen(newText);
			size_t count = newTextLength < room ? newTextLength : room;

			memmove(text + position + count, text + position, length - position + 1);
			memcpy(text + position, newText, count);
			return newTextLength <= room;
		}

		inline bool CutRangedIn(char *text, size_t capacity, int startIndex, int count)
		{
			if (capacity == 0) return false;
			long long length = (long long)BoundedLength(text, capacity);

			long long first = startIndex;
			long long end = (long long)startIndex + count;
			bool isInside = startIndex >= 0 && count >= 0 && end <= length;

			if (first < 0) first = 0;
			if (end > length) end = length;
			if (end <= first) return isInside;

			memmove(text + first, text + end, (size_t)(length - end + 1));
			return isInside;
		}

		inline char *NextTemporary()
		{
			static char buffers[8][STRING_FORMAT_CAPACITY];
			static int next;

			char *text = buffers[next];
			next = (next + 1) % 8;
			text[0] = '\0';
			return text;
		}

		inline const char *ChangeCase(const char *text, bool toUpper)
		{
			char *result = NextTemporary();

			size_t i = 0;
			for (; text && text[i] != '\0' && i < STRING_FORMAT_CAPACITY - 1; ++i)
			{
				result[i] = (char)(toUpper ? toupper((unsigned char)text[i]) : tolower((unsigned char)text[i]));
			}
			result[i] = '\0';
			return result;
		}
	}

	// Most characters text can hold.
	//   String::Capacity(Name);   // 64 for String(64)
	template<size_t N> inline unsigned int Capacity(const char (&)[N])
	{
		return (unsigned int)N - 1;
	}

	// Replaces text with newText.
	//   String::Set(Name, "Hero");
	template<size_t N> inline bool Set(char (&text)[N], const char *newText)
	{
		return Internal::Write(text, N, 0, newText);
	}

	// Adds newText at the end.
	//   String::Expand(Name, " the Brave");   // "Hero" -> "Hero the Brave"
	template<size_t N> inline bool Expand(char (&text)[N], const char *newText)
	{
		return Internal::ExpandIn(text, N, newText);
	}

	// Inserts newText before the character at index.
	//   String::InsertAt(Name, 0, "Sir ");   // "Hero" -> "Sir Hero"
	template<size_t N> inline bool InsertAt(char (&text)[N], int index, const char *newText)
	{
		return Internal::InsertIn(text, N, index, newText);
	}

	// Removes count characters, starting at startIndex. False if the range went outside the text.
	//   String::CutRanged(Name, 0, 4);   // "Sir Hero" -> "Hero"
	template<size_t N> inline bool CutRanged(char (&text)[N], int startIndex, int count)
	{
		return Internal::CutRangedIn(text, N, startIndex, count);
	}

	// Removes the characters from startIndex to endIndex, both included.
	//   String::CutFromTo(Name, 0, 3);   // "Sir Hero" -> "Hero"
	template<size_t N> inline bool CutFromTo(char (&text)[N], int startIndex, int endIndex)
	{
		return Internal::CutRangedIn(text, N, startIndex, endIndex - startIndex + 1);
	}

	// Makes text empty.
	//   String::Clear(Name);
	template<size_t N> inline void Clear(char (&text)[N])
	{
		text[0] = '\0';
	}

	// printf, into a temporary.
	//   SDL_RenderDebugText(renderer, 10, 10, String::Format("Score: %d", score));
	//   String::Set(Label, String::Format("%s x%d", item, count));   // to keep it
	String_PrintfFormat(1, 2)
	inline const char *Format(const char *format, ...)
	{
		if (!format) return "";
		char *text = Internal::NextTemporary();

		va_list args;
		va_start(args, format);
		int count = vsnprintf(text, STRING_FORMAT_CAPACITY, format, args);
		va_end(args);

		if (count < 0) text[0] = '\0';
		return text;
	}

	// text in upper case, as a temporary.
	//   String::ToUpper("hero");   // "HERO"
	inline const char *ToUpper(const char *text)
	{
		return Internal::ChangeCase(text, true);
	}

	// text in lower case, as a temporary.
	//   String::ToLower("HERO");   // "hero"
	inline const char *ToLower(const char *text)
	{
		return Internal::ChangeCase(text, false);
	}

	// Number of characters (0 for null).
	//   String::Length("Hero");   // 4
	inline unsigned int Length(const char *text)
	{
		return text ? (unsigned int)strlen(text) : 0;
	}

	// True for null or "".
	//   if (String::IsEmpty(Name)) String::Set(Name, "Player");
	inline bool IsEmpty(const char *text)
	{
		return !text || text[0] == '\0';
	}

	// True when both have the same characters (null counts as "").
	//   if (String::Equals(command, "quit")) running = false;
	inline bool Equals(const char *a, const char *b)
	{
		return strcmp(a ? a : "", b ? b : "") == 0;
	}

	// True when part appears anywhere in text.
	//   String::Contains("fireball", "fire");   // true
	inline bool Contains(const char *text, const char *part)
	{
		if (!part) return false;
		return strstr(text ? text : "", part) != null;
	}

	// True when text begins with part.
	//   String::StartsWith("Level01", "Level");   // true
	inline bool StartsWith(const char *text, const char *part)
	{
		if (!part) return false;
		return strncmp(text ? text : "", part, strlen(part)) == 0;
	}

	// True when text ends with part.
	//   String::EndsWith("Hero.png", ".png");   // true
	inline bool EndsWith(const char *text, const char *part)
	{
		if (!part) return false;
		const char *safe = text ? text : "";
		size_t textLength = strlen(safe);
		size_t partLength = strlen(part);
		return partLength <= textLength && strcmp(safe + textLength - partLength, part) == 0;
	}

	// text without spaces, tabs or newlines at either end, as a temporary.
	//   String::Trim("  hi \n");   // "hi"
	inline const char *Trim(const char *text)
	{
		char *result = Internal::NextTemporary();
		if (!text) return result;

		while (*text && isspace((unsigned char)*text)) text++;
		size_t length = strlen(text);
		while (length > 0 && isspace((unsigned char)text[length - 1])) length--;
		if (length > STRING_FORMAT_CAPACITY - 1) length = STRING_FORMAT_CAPACITY - 1;

		memcpy(result, text, length);
		result[length] = '\0';
		return result;
	}

	// Index where part first appears in text, or invalid.
	//   String::IndexOfPart("key=value", "=");   // 3
	inline int IndexOfPart(const char *text, const char *part)
	{
		if (!part) return invalid;
		const char *safe = text ? text : "";
		const char *found = strstr(safe, part);
		return found ? (int)(found - safe) : invalid;
	}

	// count characters starting at startIndex, as a temporary.
	//   String::GetRanged("Hello", 1, 3);   // "ell"
	inline const char *GetRanged(const char *text, int startIndex, int count)
	{
		char *result = Internal::NextTemporary();
		if (!text || count <= 0) return result;

		int length = (int)strlen(text);
		int first = startIndex < 0 ? 0 : (startIndex > length ? length : startIndex);
		int last = count > length - startIndex ? length : startIndex + count;
		if (last > first + STRING_FORMAT_CAPACITY - 1) last = first + STRING_FORMAT_CAPACITY - 1;
		if (last < first) last = first;

		memcpy(result, text + first, (size_t)(last - first));
		result[last - first] = '\0';
		return result;
	}

	// The characters from startIndex to endIndex, both included, as a temporary.
	//   String::GetFromTo("Hello", 1, 3);   // "ell"
	inline const char *GetFromTo(const char *text, int startIndex, int endIndex)
	{
		return GetRanged(text, startIndex, endIndex - startIndex + 1);
	}
}

template<typename T, size_t Length> struct Utilities_Slots
{
	T Items[Length] = {};
	bool Used[Length] = {};

	T &operator[](int index) { return Items[index]; }
	const T &operator[](int index) const { return Items[index]; }
	T *begin() { return Items; }
	T *end() { return Items + Length; }
	const T *begin() const { return Items; }
	const T *end() const { return Items + Length; }
};

template<typename T, size_t Length, bool Sized> struct Utilities_SlotsOf { typedef Utilities_Slots<T, Length> Type; };
template<typename T, size_t Length> struct Utilities_SlotsOf<T, Length, false> { typedef T Type[]; };
template<typename T, size_t Length, bool Sized> using Utilities_ArraySlots = typename Utilities_SlotsOf<T, Length, Sized>::Type;

// Array(Enemy, 64) Enemies; has 64 slots, each used or empty. An item keeps its slot until it is removed,
// and any value can be stored. Removing an item resets its slot to {}. Put items in with Add, InsertAt or
// SetAt: writing Enemies[i] = enemy directly doesn't mark slot i used. Enemies[i] reads a slot, and
// for (Enemy &enemy : Enemies) visits every slot, empty ones too.
//   Array(Enemy, 64) Enemies = {};
//   Array::Add(Enemies, Enemy{});
//   for (int i = 0; i < (int)Array::Capacity(Enemies); i++)
//       if (!Array::IsEmptyAt(Enemies, i)) Enemies[i].Health -= 1;
#define Array(type, ...)	Utilities_ArraySlots<type, (__VA_ARGS__ + 0), (sizeof(#__VA_ARGS__) > 1)>

namespace Array
{
	namespace Internal
	{
		template<typename T> struct Same { typedef T Type; };

		inline int SetOut(int *outIndex, int index)
		{
			if (outIndex) *outIndex = index;
			return index;
		}

		inline bool StringEquals(const char *a, const char *b)
		{
			return a && b && strcmp(a, b) == 0;
		}

		template<typename T> inline bool Equals(const T &a, const T &b)
		{
			static_assert(requires { a == b; }, "T needs ==, like: bool operator==(const T &) const = default;");
			return a == b;
		}

		template<typename T, size_t N> inline void Empty(Utilities_Slots<T, N> &array, size_t index)
		{
			array.Items[index] = T{};
			array.Used[index] = false;
		}
	}

	// Number of slots.
	//   Array::Capacity(Enemies);   // 64 for Array(Enemy, 64)
	template<typename T, size_t N> inline unsigned int Capacity(const Utilities_Slots<T, N> &)
	{
		return (unsigned int)N;
	}

	// True when slot index holds no item, or is out of range.
	//   if (!Array::IsEmptyAt(Enemies, i)) Enemies[i].Health -= 1;
	template<typename T, size_t N> inline bool IsEmptyAt(const Utilities_Slots<T, N> &array, int index)
	{
		return index < 0 || index >= (int)N || !array.Used[index];
	}

	// Number of used slots.
	//   if (Array::Length(Enemies) == 0) won = true;
	template<typename T, size_t N> inline unsigned int Length(const Utilities_Slots<T, N> &array)
	{
		unsigned int count = 0;
		for (size_t i = 0; i < N; ++i)
		{
			if (array.Used[i]) count++;
		}
		return count;
	}

	// Puts value in the first empty slot. Returns the stored item, or null when full. outIndex gets the slot.
	//   Enemy *spawned = Array::Add(Enemies, Enemy{});
	//   int slot; Array::Add(Enemies, enemy, &slot);
	template<typename T, size_t N> inline T *Add(Utilities_Slots<T, N> &array, const typename Internal::Same<T>::Type &value, int *outIndex = null)
	{
		for (int i = 0; i < (int)N; ++i)
		{
			if (!array.Used[i])
			{
				array.Items[i] = value;
				array.Used[i] = true;
				Internal::SetOut(outIndex, i);
				return &array.Items[i];
			}
		}

		Internal::SetOut(outIndex, invalid);
		return null;
	}

	// Puts value at index, only if that slot is empty. Returns the stored item, or null.
	//   Array::InsertAt(Inventory, 3, sword);
	template<typename T, size_t N> inline T *InsertAt(Utilities_Slots<T, N> &array, int index, const typename Internal::Same<T>::Type &value)
	{
		if (index < 0 || index >= (int)N || array.Used[index]) return null;
		array.Items[index] = value;
		array.Used[index] = true;
		return &array.Items[index];
	}

	// Puts value in count slots from start, overwriting them.
	//   Array::SetRanged(Tiles, 0, 10, Tile{});   // slots 0 to 9
	template<typename T, size_t N> inline void SetRanged(Utilities_Slots<T, N> &array, int start, int count, const typename Internal::Same<T>::Type &value)
	{
		if (start < 0)
		{
			count += start;
			start = 0;
		}
		if (count > (int)N - start) count = (int)N - start;
		for (int i = 0; i < count; ++i)
		{
			array.Items[start + i] = value;
			array.Used[start + i] = true;
		}
	}

	// Puts value in the slots from start to end, both included, overwriting them.
	//   Array::SetFromTo(Tiles, 0, 9, Tile{});
	template<typename T, size_t N> inline void SetFromTo(Utilities_Slots<T, N> &array, int start, int end, const typename Internal::Same<T>::Type &value)
	{
		SetRanged(array, start, end - start + 1, value);
	}

	// Puts value at index, overwriting what's there. Returns the stored item, or null when out of range.
	//   Array::SetAt(Inventory, 3, sword);
	template<typename T, size_t N> inline T *SetAt(Utilities_Slots<T, N> &array, int index, const typename Internal::Same<T>::Type &value)
	{
		if (index < 0 || index >= (int)N) return null;
		array.Items[index] = value;
		array.Used[index] = true;
		return &array.Items[index];
	}

	// Puts value in every slot.
	//   Array::SetAll(Tiles, Tile{});
	template<typename T, size_t N> inline void SetAll(Utilities_Slots<T, N> &array, const typename Internal::Same<T>::Type &value)
	{
		SetRanged(array, 0, (int)N, value);
	}

	// Empties slot index and returns what was in it ({} if it was already empty).
	//   Enemy removed = Array::RemoveAt(Enemies, 3);
	template<typename T, size_t N> inline T RemoveAt(Utilities_Slots<T, N> &array, int index)
	{
		T removed = {};

		if (index >= 0 && index < (int)N && array.Used[index])
		{
			removed = array.Items[index];
			Internal::Empty(array, (size_t)index);
		}

		return removed;
	}

	// Slot of the first item equal to value (with ==; text by its characters), or invalid.
	//   int slot = Array::IndexOf(Names, "Hero");
	template<typename T, size_t N> inline int IndexOf(const Utilities_Slots<T, N> &array, const typename Internal::Same<T>::Type &value)
	{
		for (int i = 0; i < (int)N; ++i)
		{
			if (array.Used[i] && Internal::Equals<T>(array.Items[i], value)) return i;
		}
		return invalid;
	}

	template<size_t N> inline int IndexOf(const Utilities_Slots<char *, N> &array, const char *value)
	{
		for (int i = 0; i < (int)N; ++i)
		{
			if (array.Used[i] && Internal::StringEquals(array.Items[i], value)) return i;
		}
		return invalid;
	}

	template<size_t N> inline int IndexOf(const Utilities_Slots<const char *, N> &array, const char *value)
	{
		for (int i = 0; i < (int)N; ++i)
		{
			if (array.Used[i] && Internal::StringEquals(array.Items[i], value)) return i;
		}
		return invalid;
	}

	// True when an item equals value (see IndexOf). outIndex gets its slot.
	//   if (Array::Contains(Unlocked, levelId)) Load(levelId);
	template<typename T, size_t N, typename Value> inline bool Contains(const Utilities_Slots<T, N> &array, const Value &value, int *outIndex = null)
	{
		return Internal::SetOut(outIndex, IndexOf(array, value)) >= 0;
	}

	// Empties the first slot equal to value (see IndexOf) and returns what was in it. outIndex gets the slot.
	//   Array::Remove(Unlocked, levelId);
	template<typename T, size_t N, typename Value> inline T Remove(Utilities_Slots<T, N> &array, const Value &value, int *outIndex = null)
	{
		const int index = IndexOf(array, value);
		Internal::SetOut(outIndex, index);
		return RemoveAt(array, index);
	}

	// Slot of the first item for which condition(item) is true, or invalid.
	//   int boss = Array::FirstIndex(Enemies, [](Enemy &enemy) { return enemy.IsBoss; });
	template<typename T, size_t N, typename Condition> inline int FirstIndex(Utilities_Slots<T, N> &array, Condition condition)
	{
		for (int i = 0; i < (int)N; ++i)
		{
			if (!array.Used[i]) continue;
			if (condition(array.Items[i])) return i;
		}
		return invalid;
	}

	// Empties the first slot for which condition(item) is true and returns what was in it. outIndex gets the slot.
	//   Array::RemoveFirst(Enemies, [](Enemy &enemy) { return enemy.IsBoss; });
	template<typename T, size_t N, typename Condition> inline T RemoveFirst(Utilities_Slots<T, N> &array, Condition condition, int *outIndex = null)
	{
		const int index = FirstIndex(array, condition);
		Internal::SetOut(outIndex, index);
		return RemoveAt(array, index);
	}

	// Empties every slot for which condition(item) is true. Returns how many.
	//   Array::RemoveAll(Enemies, [](Enemy &enemy) { return enemy.Health <= 0; });
	template<typename T, size_t N, typename Condition> inline unsigned int RemoveAll(Utilities_Slots<T, N> &array, Condition condition)
	{
		unsigned int removed = 0;
		for (int i = 0; i < (int)N; ++i)
		{
			if (!array.Used[i]) continue;
			if (condition(array.Items[i]))
			{
				Internal::Empty(array, (size_t)i);
				removed++;
			}
		}
		return removed;
	}

	// Writes the slots for which condition(item) is true into outIndexes, the rest set to invalid. Returns how many.
	//   int hits[8]; unsigned int count = Array::AllIndexes(Enemies, [](Enemy &enemy) { return enemy.Hit; }, hits);
	template<typename T, size_t N, size_t M, typename Condition> inline unsigned int AllIndexes(Utilities_Slots<T, N> &array, Condition condition, int (&outIndexes)[M])
	{
		unsigned int count = 0;
		for (size_t k = 0; k < M; ++k) outIndexes[k] = invalid;
		for (int i = 0; i < (int)N && count < (unsigned int)M; ++i)
		{
			if (!array.Used[i]) continue;
			if (condition(array.Items[i])) outIndexes[count++] = i;
		}
		return count;
	}

	// Empties count slots from start. False if the range went outside the array.
	//   Array::RemoveRanged(Tiles, 0, 10);   // slots 0 to 9
	template<typename T, size_t N> inline bool RemoveRanged(Utilities_Slots<T, N> &array, int start, int count)
	{
		bool isInside = start >= 0 && count >= 0 && start <= (int)N - count;
		int first = start < 0 ? 0 : start;
		int last = count > (int)N - start ? (int)N : start + count;
		for (int i = first; i < last; ++i) Internal::Empty(array, (size_t)i);
		return isInside;
	}

	// Empties the slots from start to end, both included.
	//   Array::RemoveFromTo(Tiles, 0, 9);
	template<typename T, size_t N> inline bool RemoveFromTo(Utilities_Slots<T, N> &array, int start, int end)
	{
		return end >= start && RemoveRanged(array, start, end - start + 1);
	}

	// Number of empty slots before the last used one.
	//   if (Array::GapCounts(Enemies) > 0) Array::LineUp(Enemies);
	template<typename T, size_t N> inline unsigned int GapCounts(const Utilities_Slots<T, N> &array)
	{
		int last = invalid;
		for (int i = (int)N - 1; i >= 0; --i)
		{
			if (array.Used[i])
			{
				last = i;
				break;
			}
		}

		unsigned int gaps = 0;
		for (int i = 0; i < last; ++i)
		{
			if (!array.Used[i]) gaps++;
		}
		return gaps;
	}

	// Moves the items into the first slots, keeping their order, so there are no gaps. Returns how many.
	//   unsigned int count = Array::LineUp(Enemies);   // items are now in slots 0 to count - 1
	template<typename T, size_t N> inline unsigned int LineUp(Utilities_Slots<T, N> &array)
	{
		int write = 0;
		for (int read = 0; read < (int)N; ++read)
		{
			if (!array.Used[read]) continue;
			if (read != write) array.Items[write] = array.Items[read];
			write++;
		}
		for (int i = 0; i < (int)N; ++i)
		{
			if (i < write) array.Used[i] = true;
			else Internal::Empty(array, (size_t)i);
		}
		return (unsigned int)write;
	}

	// LineUp, then sorts so before(a, b) is true when a goes first (equal items keep their order). Returns how many.
	//   Array::Sort(Scores, [](const Score &a, const Score &b) { return a.Points > b.Points; });
	template<typename T, size_t N, typename Order> inline unsigned int Sort(Utilities_Slots<T, N> &array, Order before)
	{
		unsigned int count = LineUp(array);
		for (int k = 1; k < (int)count; ++k)
		{
			T key = array.Items[k];
			int j = k - 1;
			while (j >= 0 && before(key, array.Items[j]))
			{
				array.Items[j + 1] = array.Items[j];
				j--;
			}
			array.Items[j + 1] = key;
		}
		return count;
	}

	// Copies slot by slot, empty slots included. Extra destination slots are emptied. Returns how many were copied.
	//   Array::Copy(Enemies, Backup);   // for the same size, Backup = Enemies works too
	template<typename T, size_t N, size_t M> inline unsigned int Copy(const Utilities_Slots<T, N> &source, Utilities_Slots<T, M> &destination)
	{
		size_t count = N < M ? N : M;
		if ((const void *)&source == (const void *)&destination) return (unsigned int)count;
		for (size_t i = 0; i < count; ++i)
		{
			destination.Items[i] = source.Items[i];
			destination.Used[i] = source.Used[i];
		}
		for (size_t i = count; i < M; ++i) Internal::Empty(destination, i);
		return (unsigned int)count;
	}

	// Swaps two slots, empty or not. False when out of range.
	//   Array::Swap(Inventory, 0, 5);
	template<typename T, size_t N> inline bool Swap(Utilities_Slots<T, N> &array, int a, int b)
	{
		if (a < 0 || b < 0 || a >= (int)N || b >= (int)N) return false;
		if (a != b)
		{
			T temporary = array.Items[a];
			array.Items[a] = array.Items[b];
			array.Items[b] = temporary;
			bool used = array.Used[a];
			array.Used[a] = array.Used[b];
			array.Used[b] = used;
		}
		return true;
	}

	// Empties every slot.
	//   Array::Clear(Enemies);
	template<typename T, size_t N> inline void Clear(Utilities_Slots<T, N> &array)
	{
		for (size_t i = 0; i < N; ++i) Internal::Empty(array, i);
	}

	// Reverses the order of all slots, empty ones included.
	//   Array::Reverse(Inventory);
	template<typename T, size_t N> inline void Reverse(Utilities_Slots<T, N> &array)
	{
		for (int i = 0, j = (int)N - 1; i < j; ++i, --j) Swap(array, i, j);
	}
}

// CArray:: works on plain C arrays (int Scores[8];), where every element counts as used.
//   int Scores[8] = {};
//   for (int i = 0; i < (int)CArray::Length(Scores); i++) Scores[i] = i;
namespace CArray
{
	// Number of elements.
	//   CArray::Length(Scores);   // 8 for int Scores[8]
	template<typename T, size_t N> inline unsigned int Length(const T (&)[N])
	{
		return (unsigned int)N;
	}

	// Index of the first element equal to value (with ==; text by its characters), or invalid.
	//   int index = CArray::IndexOf(Scores, 100);
	template<typename T, size_t N, typename Value> inline int IndexOf(const T (&array)[N], const Value &value)
	{
		for (int i = 0; i < (int)N; ++i)
		{
			if constexpr (requires { Array::Internal::StringEquals(array[i], value); })
			{
				if (Array::Internal::StringEquals(array[i], value)) return i;
			}
			else if (Array::Internal::Equals<T>(array[i], value)) return i;
		}
		return invalid;
	}

	// True when an element equals value (see IndexOf). outIndex gets its index.
	//   if (CArray::Contains(Scores, 100)) perfect = true;
	template<typename T, size_t N, typename Value> inline bool Contains(const T (&array)[N], const Value &value, int *outIndex = null)
	{
		return Array::Internal::SetOut(outIndex, IndexOf(array, value)) >= 0;
	}

	// Index of the first element for which condition(element) is true, or invalid.
	//   int first = CArray::FirstIndex(Scores, [](int score) { return score > 50; });
	template<typename T, size_t N, typename Condition> inline int FirstIndex(T (&array)[N], Condition condition)
	{
		for (int i = 0; i < (int)N; ++i)
		{
			if (condition(array[i])) return i;
		}
		return invalid;
	}

	// Writes the indexes for which condition(element) is true into outIndexes, the rest set to invalid. Returns how many.
	//   int high[8]; unsigned int count = CArray::AllIndexes(Scores, [](int score) { return score > 50; }, high);
	template<typename T, size_t N, size_t M, typename Condition> inline unsigned int AllIndexes(T (&array)[N], Condition condition, int (&outIndexes)[M])
	{
		unsigned int count = 0;
		for (size_t k = 0; k < M; ++k) outIndexes[k] = invalid;
		for (int i = 0; i < (int)N && count < (unsigned int)M; ++i)
		{
			if (condition(array[i])) outIndexes[count++] = i;
		}
		return count;
	}

	// Puts value in count elements from start, overwriting them.
	//   CArray::SetRanged(Scores, 0, 4, 0);   // elements 0 to 3
	template<typename T, size_t N> inline void SetRanged(T (&array)[N], int start, int count, const typename Array::Internal::Same<T>::Type &value)
	{
		if (start < 0)
		{
			count += start;
			start = 0;
		}
		if (count > (int)N - start) count = (int)N - start;
		for (int i = 0; i < count; ++i) array[start + i] = value;
	}

	// Puts value in the elements from start to end, both included, overwriting them.
	//   CArray::SetFromTo(Scores, 0, 3, 0);
	template<typename T, size_t N> inline void SetFromTo(T (&array)[N], int start, int end, const typename Array::Internal::Same<T>::Type &value)
	{
		SetRanged(array, start, end - start + 1, value);
	}

	// Puts value in every element.
	//   CArray::SetAll(Scores, 0);
	template<typename T, size_t N> inline void SetAll(T (&array)[N], const typename Array::Internal::Same<T>::Type &value)
	{
		SetRanged(array, 0, (int)N, value);
	}

	// Sorts so before(a, b) is true when a goes first (equal elements keep their order).
	//   CArray::Sort(Scores, [](const int &a, const int &b) { return a > b; });
	template<typename T, size_t N, typename Order> inline void Sort(T (&array)[N], Order before)
	{
		for (int k = 1; k < (int)N; ++k)
		{
			T key = array[k];
			int j = k - 1;
			while (j >= 0 && before(key, array[j]))
			{
				array[j + 1] = array[j];
				j--;
			}
			array[j + 1] = key;
		}
	}

	// Copies element by element, as many as both hold; extra destination elements are left as they are. Returns how many.
	//   CArray::Copy(Scores, Backup);
	template<typename T, size_t N, size_t M> inline unsigned int Copy(const T (&source)[N], T (&destination)[M])
	{
		size_t count = N < M ? N : M;
		if ((const void *)&source == (const void *)&destination) return (unsigned int)count;
		for (size_t i = 0; i < count; ++i) destination[i] = source[i];
		return (unsigned int)count;
	}

	// Swaps two elements. False when out of range.
	//   CArray::Swap(Scores, 0, 7);
	template<typename T, size_t N> inline bool Swap(T (&array)[N], int a, int b)
	{
		if (a < 0 || b < 0 || a >= (int)N || b >= (int)N) return false;
		if (a != b)
		{
			T temporary = array[a];
			array[a] = array[b];
			array[b] = temporary;
		}
		return true;
	}

	// Reverses the order of the elements.
	//   CArray::Reverse(Scores);
	template<typename T, size_t N> inline void Reverse(T (&array)[N])
	{
		for (int i = 0, j = (int)N - 1; i < j; ++i, --j) Swap(array, i, j);
	}

	// Resets every element to {}.
	//   CArray::Clear(Scores);
	template<typename T, size_t N> inline void Clear(T (&array)[N])
	{
		for (size_t i = 0; i < N; ++i) array[i] = T{};
	}
}
