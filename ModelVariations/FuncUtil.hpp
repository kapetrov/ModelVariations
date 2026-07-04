#pragma once

#include "LoadedModules.hpp"

#include <algorithm>
#include <iterator>
#include <charconv>
#include <type_traits>
#include <utility>
#include <vector>

#include <CGeneral.h>
#include <CMessages.h>

#include <ntstatus.h>


inline bool isGameHOODLUM()
{
    return (plugin::GetGameVersion() == GAME_10US_HOODLUM);
}

inline bool isGameCompact()
{
    return (plugin::GetGameVersion() == GAME_10US_COMPACT);
}

inline CVector2D convert3DVectorTo2D(const CVector& vec)
{
    return { vec.x, vec.y };
}

inline void printMessage(const char* message, unsigned int time)
{
    CMessages::AddMessageJumpQ(const_cast<char*>(message), time, 0, false);
}

inline std::string getFullPath(const std::string& filename)
{
    return filename.find(':') != std::string::npos ? filename : (LoadedModules::GetSelfDirectory() + '\\' + filename);
}

inline std::string printFilenameWithBorder(const std::string &name, const char ch = '#') 
{
    std::string outString;
    size_t line_width = name.size() + 6; // "## " + name + " ##"


    for (size_t i = 0;i<line_width;i++)
        outString += ch;

    outString += "\n";
    outString += std::string(2, ch) + " " + name + " " + std::string(2, ch);
    outString += "\n";

    for (size_t i = 0;i<line_width;i++)
        outString += ch;

    return outString;
}

inline bool fileExists(const std::string& filename)
{
    return GetFileAttributes(getFullPath(filename).c_str()) != INVALID_FILE_ATTRIBUTES;
}

inline bool isTimeInRange(int timeNow, int timeStart, int timeEnd)
{
    if (timeStart <= timeEnd) // Normal range (same day)
        return timeNow >= timeStart && timeNow <= timeEnd;

    return timeNow >= timeStart || timeNow <= timeEnd; // Wrap-around past midnight
}

inline std::string getDatetime(bool printDate, bool printTime, bool printMs)
{
    SYSTEMTIME s;
    GetSystemTime(&s);

    auto day = std::to_string(s.wDay);
    auto month = std::to_string(s.wMonth);
    auto year = std::to_string(s.wYear);

    auto z = [](int n, int width)
    {
        std::string r = std::to_string(n);
        return std::string(width - r.size(), '0') + r;
    };

    return (printDate ? day + "/" + month + "/" + year + (printTime ? " " : "") : "") +
           (printTime ? z(s.wHour, 2) + ":" + z(s.wMinute, 2) + ":" + z(s.wSecond, 2) + (printMs ? "." + z(s.wMilliseconds, 3) : "") : "");
}

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

inline std::string mvsprintf(const char* fmt, va_list ap)
{
    if (!fmt) return {};
    std::string out;

    auto utoa = [](unsigned long long v, unsigned base, bool upper) {
        const char* d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
        std::string s;
        do {
            s.insert(s.begin(), d[v % base]);
            v /= base;
        } while (v);
        return s;
    };

    auto int_prec = [](std::string s, int p) {
        if (p < 0) return s;
        bool neg = !s.empty() && s[0] == '-';
        int digits = (int)s.size() - neg;
        if (digits < p) s.insert(neg ? 1 : 0, p - digits, '0');
        return s;
    };

    auto width_pad = [](std::string s, int w, bool zero, bool numeric) {
        if (w <= (int)s.size()) return s;
        int n = w - (int)s.size();

        if (zero && numeric && !s.empty() && s[0] == '-')
            return "-" + std::string(n, '0') + s.substr(1);

        return std::string(n, zero && numeric ? '0' : ' ') + s;
    };

    for (; *fmt; ++fmt) {
        if (*fmt != '%') {
            out += *fmt;
            continue;
        }

        ++fmt;
        if (!*fmt) {
            out += '%';
            break;
        }
        if (*fmt == '%') {
            out += '%';
            continue;
        }

        bool zero = false;
        if (*fmt == '0') {
            zero = true;
            ++fmt;
        }

        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            ++fmt;
        }

        int prec = -1;
        if (*fmt == '.') {
            prec = 0;
            ++fmt;
            while (*fmt >= '0' && *fmt <= '9') {
                prec = prec * 10 + (*fmt - '0');
                ++fmt;
            }
        }

        std::string s;

        switch (*fmt) {
        case 'd': {
            long long v = va_arg(ap, int);
            bool neg = v < 0;
            unsigned long long u = neg ? (unsigned long long)(-v) : (unsigned long long)v;
            s = prec == 0 && u == 0 ? "" : utoa(u, 10, false);
            if (neg) s.insert(s.begin(), '-');
            s = int_prec(s, prec);
            out += width_pad(s, width, zero && prec < 0, true);
            break;
        }

        case 'u': {
            unsigned v = va_arg(ap, unsigned);
            s = prec == 0 && v == 0 ? "" : utoa(v, 10, false);
            s = int_prec(s, prec);
            out += width_pad(s, width, zero && prec < 0, true);
            break;
        }

        case 'x':
        case 'X': {
            unsigned v = va_arg(ap, unsigned);
            s = prec == 0 && v == 0 ? "" : utoa(v, 16, *fmt == 'X');
            s = int_prec(s, prec);
            out += width_pad(s, width, zero && prec < 0, true);
            break;
        }

        case 's': {
            const char* p = va_arg(ap, const char*);
            s = p ? p : "(null)";
            if (prec >= 0 && prec < (int)s.size()) s.resize(prec);
            out += width_pad(s, width, false, false);
            break;
        }

        case 'c': {
            s += char(va_arg(ap, int));
            out += width_pad(s, width, false, false);
            break;
        }

        case 'f': {
            double v = va_arg(ap, double);
            int p = prec >= 0 ? prec : 6;
            if (p > 18) p = 18;

            bool neg = v < 0;
            if (neg) v = -v;

            unsigned long long scale = 1;
            for (int i = 0; i < p; ++i) scale *= 10;

            unsigned long long all = (unsigned long long)(v * scale + 0.5);

            s = utoa(all / scale, 10, false);

            if (p > 0) {
                std::string fs = utoa(all % scale, 10, false);
                if ((int)fs.size() < p)
                    fs.insert(0, p - fs.size(), '0');
                s += "." + fs;
            }

            if (neg) s.insert(s.begin(), '-');

            out += width_pad(s, width, zero, true);
            break;
        }

        default:
            out += '%';
            out += *fmt;
            break;
        }
    }

    return out;
}

inline std::string msprintf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::string s = mvsprintf(fmt, ap);
    va_end(ap);
    return s;
}

inline char* copyString(char* dest, const char* src, size_t n) //Does not null-terminate
{
    if (dest == nullptr || src == nullptr)
        return nullptr;

    size_t i = 0;

    while (i < n && src[i] != '\0')
    {
        dest[i] = src[i];
        ++i;
    }

    return dest;
}

inline char toUpper(char c)
{
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

inline std::string bytesToString(std::uintptr_t address, unsigned int nBytes)
{
    const unsigned char* c = reinterpret_cast<unsigned char*>(address);
    std::string result;

    for (unsigned int i = 0; i < nBytes; ++i)
    {
        if (i > 0)
            result += ' ';
        result += msprintf("%02X", c[i]);
    }

    return result;
}

inline std::string fileToString(const std::string &filename)
{
    std::string str;

    HANDLE hFile = CreateFile(getFullPath(filename).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return str;

    auto filesize = GetFileSize(hFile, NULL);
    if (filesize == INVALID_FILE_SIZE || filesize == 0)
    {
        CloseHandle(hFile);
        return str;
    }

    str.resize(filesize);
    DWORD lpNumberOfBytesRead = 0;
    ReadFile(hFile, &str[0], filesize, &lpNumberOfBytesRead, NULL);

    CloseHandle(hFile);
    return str;
}

inline std::string getFilenameFromPath(const std::string &path)
{
    return path.substr(path.find_last_of("/\\") + 1);
}

inline bool strcasestr(std::string src, std::string sub)
{
    std::for_each(src.begin(), src.end(), [](char& c) {
        c = toUpper(c);
    });

    std::for_each(sub.begin(), sub.end(), [](char& c) {
        c = toUpper(c);
    });

    if (src.find(sub) != std::string::npos)
        return true;

    return false;
}

inline bool strcasecmp(std::string_view s1, std::string_view s2)
{
    if (s1.empty())
        return s2.empty();

    if (s1.size() != s2.size())
        return false;

    for (size_t i = 0; i < s1.size(); i++)
    {
        if (toUpper(s1[i]) != toUpper(s2[i]))
            return false;
    }

    return true;
}

inline std::vector<std::string> splitString(const std::string &s, char separator)
{
    std::vector<std::string> out;

    std::size_t start = 0;
    while (start <= s.size())
    {
        const std::size_t pos = s.find(separator, start);
        const std::size_t end = (pos == std::string::npos) ? s.size() : pos;

        if (end > start) // non-empty token
            out.emplace_back(s.substr(start, end - start));

        if (pos == std::string::npos)
            break;

        start = pos + 1;
    }

    return out;
}

inline std::vector<std::string> splitString(const std::string& s, const std::string& separators)
{
    std::vector<std::string> out;

    std::size_t start = 0;

    while (start < s.size())
    {
        // Skip leading separators
        start = s.find_first_not_of(separators, start);
        if (start == std::string::npos) break;

        // Find end of token
        std::size_t end = s.find_first_of(separators, start);
        if (end == std::string::npos)
        {
            out.emplace_back(s.substr(start));
            break;
        }

        out.emplace_back(s.substr(start, end - start));
        start = end + 1;
    }

    return out;
}

inline std::string trimString(const std::string& str) 
{
    size_t first = str.find_first_not_of(" \t\n\r");
    size_t last = str.find_last_not_of(" \t\n\r");

    if (first == std::string::npos)
        return "";

    return str.substr(first, (last - first + 1));
}

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

inline void vectorfilterVector(std::vector<unsigned short>& vec, const std::vector<unsigned short>& filterVec)
{
    if (filterVec.empty())
        return;

    std::vector<unsigned short> vec2;

    for (auto i : vec)
        if (std::find(filterVec.begin(), filterVec.end(), i) != filterVec.end())
            vec2.push_back(i);

    if (!vec2.empty())
        vec = std::move(vec2);
}

inline unsigned short vectorGetRandom(const std::vector<unsigned short>& vec)
{
    if (vec.empty())
        return 0;
    return vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())];
}

inline bool vectorHasId(const std::vector<unsigned short>& vec, int id)
{
    if (vec.size() < 1)
        return false;

    return std::find(vec.begin(), vec.end(), id) != vec.end();
}

inline bool vectorPushUnique(std::vector<unsigned short>& vec, unsigned short value)
{
    if (std::find(vec.begin(), vec.end(), value) == vec.end())
    {
        vec.push_back(value);
        return true;
    }

    return false;
}

inline std::vector<unsigned short> vectorUnion(const std::vector<unsigned short>& vec1, const std::vector<unsigned short>& vec2)
{
    if (vec1.empty())
        return vec2;

    if (vec2.empty())
        return vec1;

    std::vector<unsigned short> vecOut;
    std::set_union(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(), std::back_inserter(vecOut));
    return vecOut;
}
