#include "Helpers.hpp"


#include <algorithm>
#include <iterator>
#include <utility>

#include <CMessages.h>

#include <ntstatus.h>


bool isGameHOODLUM()
{
    return (plugin::GetGameVersion() == GAME_10US_HOODLUM);
}

bool isGameCompact()
{
    return (plugin::GetGameVersion() == GAME_10US_COMPACT);
}

CVector2D convert3DVectorTo2D(const CVector& vec)
{
    return { vec.x, vec.y };
}

std::string getFullPath(const std::string& filename)
{
    return filename.find(':') != std::string::npos ? filename : (LoadedModules::GetSelfDirectory() + '\\' + filename);
}

std::string printFilenameWithBorder(const std::string& name, const char ch)
{
    std::string outString;
    size_t line_width = name.size() + 6; // "## " + name + " ##"


    for (size_t i = 0; i < line_width; i++)
        outString += ch;

    outString += "\n";
    outString += std::string(2, ch) + " " + name + " " + std::string(2, ch);
    outString += "\n";

    for (size_t i = 0; i < line_width; i++)
        outString += ch;

    return outString;
}

bool fileExists(const std::string& filename)
{
    return GetFileAttributes(getFullPath(filename).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool isTimeInRange(int timeNow, int timeStart, int timeEnd)
{
    if (timeStart <= timeEnd) // Normal range (same day)
        return timeNow >= timeStart && timeNow <= timeEnd;

    return timeNow >= timeStart || timeNow <= timeEnd; // Wrap-around past midnight
}

std::string getDatetime(bool printDate, bool printTime, bool printMs)
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

/////////////
// Strings //
/////////////

std::string mvsprintf(const char* fmt, va_list ap)
{
    if (!fmt) return {};

    char buf[256];

    va_list copy;
    va_copy(copy, ap);
    int n = std::vsnprintf(buf, sizeof buf, fmt, copy);
    va_end(copy);

    if (n < 0) return {};

    if ((size_t)n < sizeof buf)
        return std::string(buf, n);

    std::string out(n + 1, '\0');
    std::vsnprintf(out.data(), out.size(), fmt, ap);
    out.resize(n);

    return out;
}

std::string msprintf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::string s = mvsprintf(fmt, ap);
    va_end(ap);
    return s;
}

char* copyString(char* dest, const char* src, size_t n) //Does not null-terminate
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

char toUpper(char c)
{
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

std::string bytesToString(std::uintptr_t address, unsigned int nBytes)
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

std::string fileToString(const std::string& filename)
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
    auto success = ReadFile(hFile, &str[0], filesize, &lpNumberOfBytesRead, NULL);

    CloseHandle(hFile);

    if (!success)
        return "";

    return str;
}

std::string getFilenameFromPath(const std::string& path)
{
    return path.substr(path.find_last_of("/\\") + 1);
}

bool strcasestr(std::string src, std::string sub)
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

bool strcasecmp(std::string_view s1, std::string_view s2)
{
    while (!s1.empty() && s1.back() == '\0')
        s1.remove_suffix(1);

    while (!s2.empty() && s2.back() == '\0')
        s2.remove_suffix(1);

    if (s1.size() != s2.size())
        return false;

    for (size_t i = 0; i < s1.size(); i++)
    {
        if (toUpper(s1[i]) != toUpper(s2[i]))
            return false;
    }

    return true;
}

std::vector<std::string> splitString(const std::string& s, char separator)
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

std::vector<std::string> splitString(const std::string& s, const std::string& separators)
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

std::string trimString(const std::string& str)
{
    size_t first = str.find_first_not_of(" \t\n\r");
    size_t last = str.find_last_not_of(" \t\n\r");

    if (first == std::string::npos)
        return "";

    return str.substr(first, (last - first + 1));
}


/////////////
// Vectors //
/////////////

void vectorfilterVector(std::vector<unsigned short>& vec, const std::vector<unsigned short>& filterVec)
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

unsigned short vectorGetRandom(const std::vector<unsigned short>& vec)
{
    if (vec.empty())
        return 0;
    return vec[CGeneral::GetRandomNumberInRange(0, (int)vec.size())];
}

bool vectorHasId(const std::vector<unsigned short>& vec, int id)
{
    if (vec.size() < 1)
        return false;

    return std::find(vec.begin(), vec.end(), id) != vec.end();
}

bool vectorPushUnique(std::vector<unsigned short>& vec, unsigned short value)
{
    if (std::find(vec.begin(), vec.end(), value) == vec.end())
    {
        vec.push_back(value);
        return true;
    }

    return false;
}

std::vector<unsigned short> vectorUnion(const std::vector<unsigned short>& vec1, const std::vector<unsigned short>& vec2)
{
    if (vec1.empty())
        return vec2;

    if (vec2.empty())
        return vec1;

    std::vector<unsigned short> vecOut;
    std::set_union(vec1.begin(), vec1.end(), vec2.begin(), vec2.end(), std::back_inserter(vecOut));
    return vecOut;
}
