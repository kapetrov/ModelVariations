#pragma once

#include "LoadedModules.hpp"

#include <CGeneral.h>
#include <CVector.h>

#include <charconv>
#include <string>
#include <type_traits>
#include <vector>


bool isGameHOODLUM();
bool isGameCompact();
CVector2D convert3DVectorTo2D(const CVector& vec);
std::string getFullPath(const std::string& filename);
std::string printFilenameWithBorder(const std::string& name, const char ch = '#');
bool fileExists(const std::string& filename);
bool isTimeInRange(int timeNow, int timeStart, int timeEnd);
std::string getDatetime(bool printDate, bool printTime, bool printMs);

////////////
// Random //
////////////

template <typename T>
T rand(int min, unsigned int max)
{
    return (T)CGeneral::GetRandomNumberInRange(min, (int)max);
}

template <typename T>
bool rand()
{
    static_assert(std::is_same_v<T, bool>, "invalid type for template");

    return (bool)CGeneral::GetRandomNumberInRange(0, 2);
}


/////////////
// Strings //
/////////////

std::string mvsprintf(const char* fmt, va_list ap);
std::string msprintf(const char* fmt, ...);
char* copyString(char* dest, const char* src, size_t n);
char toUpper(char c);
std::string bytesToString(std::uintptr_t address, unsigned int nBytes);
std::string fileToString(const std::string& filename);
std::string getFilenameFromPath(const std::string& path);
bool strcasestr(std::string src, std::string sub);
bool strcasecmp(std::string_view s1, std::string_view s2);
std::vector<std::string> splitString(const std::string& s, char separator);
std::vector<std::string> splitString(const std::string& s, const std::string& separators);
std::string trimString(const std::string& str);

template <typename T>
bool fromString(std::string_view str, T& x, int base = 10)
{
    T value{};

    const char* first = str.data();
    const char* last = str.data() + str.size();

    std::from_chars_result result{};

    if constexpr (std::is_integral_v<T>)
        result = std::from_chars(first, last, value, base);
    else if constexpr (std::is_floating_point_v<T>)
        result = std::from_chars(first, last, value);
    else
        static_assert(std::is_arithmetic_v<T>, "fromString<T> only supports arithmetic types parseable by std::from_chars");

    if (result.ec != std::errc{} || result.ptr != last)
        return false;

    x = value;
    return true;
}


/////////////
// Vectors //
/////////////

void vectorfilterVector(std::vector<unsigned short>& vec, const std::vector<unsigned short>& filterVec);
unsigned short vectorGetRandom(const std::vector<unsigned short>& vec);
bool vectorHasId(const std::vector<unsigned short>& vec, int id);
bool vectorPushUnique(std::vector<unsigned short>& vec, unsigned short value);
std::vector<unsigned short> vectorUnion(const std::vector<unsigned short>& vec1, const std::vector<unsigned short>& vec2);
