#include "LoadedModules.hpp"

#include "Helpers.hpp"
#include "Log.hpp"

#include <map>

#include <ntstatus.h>

std::map<loadedModNames, bool> loadedMods;
std::vector<std::pair<std::string, MODULEINFO>> loadedModules;
std::string modDirectory;

std::pair<std::string, MODULEINFO> exeModule;


static std::string hashFile(const std::string& filename)
{
    HANDLE hFile = CreateFile(getFullPath(filename).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return "";

    static constexpr char hex[] = "0123456789abcdef";
    std::string hashString;
    auto filesize = GetFileSize(hFile, NULL);

    if (filesize != INVALID_FILE_SIZE && filesize > 0)
    {
        DWORD lpNumberOfBytesRead = 0;
        BCRYPT_ALG_HANDLE hProvider = NULL;
        BCRYPT_HASH_HANDLE ctx = NULL;
        auto filebuf = std::vector<BYTE>(filesize + 1);

        if (ReadFile(hFile, filebuf.data(), filesize, &lpNumberOfBytesRead, NULL) && lpNumberOfBytesRead == filesize)
            if (BCryptOpenAlgorithmProvider(&hProvider, BCRYPT_SHA256_ALGORITHM, NULL, 0) == STATUS_SUCCESS)
                if (BCryptCreateHash(hProvider, &ctx, NULL, 0, NULL, 0, 0) == STATUS_SUCCESS && ctx != NULL)
                {
                    auto hashArray = std::vector<BYTE>(32);
                    BCryptHashData(ctx, filebuf.data(), filesize, 0);
                    BCryptFinishHash(ctx, hashArray.data(), 32, 0);
                    BCryptDestroyHash(ctx);
                    BCryptCloseAlgorithmProvider(hProvider, 0);

                    for (BYTE i : hashArray)
                    {
                        hashString += hex[(i >> 4) & 0x0F];
                        hashString += hex[i & 0x0F];
                    }
                }
    }

    CloseHandle(hFile);
    return hashString;
}

std::pair<std::string, MODULEINFO> LoadedModules::GetModuleAtAddress(std::uintptr_t address)
{
    if (address)
        for (const auto& it : loadedModules)
        {
            uint32_t base = reinterpret_cast<uint32_t>(it.second.lpBaseOfDll);
            if (address >= base && address < base + it.second.SizeOfImage)
                return it;
        }

    return {};
}

std::pair<std::string, MODULEINFO> LoadedModules::GetModule(const std::string &name, bool exactMatch)
{
    for (const auto& i : loadedModules)
        if (exactMatch)
        {
            auto filename = getFilenameFromPath(i.first);
            if (strcasecmp(filename, name))
                return i;
        }
        else if (strcasestr(i.first, name))
            return i;

    return {};
}

const std::pair<std::string, MODULEINFO>& LoadedModules::GetExeModule()
{
    if (exeModule.first.size() || loadedModules.empty())
        return exeModule;

    std::string exeName(256, 0);
    DWORD length = GetModuleFileName(NULL, &exeName[0], 255);
    if (length == 0 || length >= 256)
        return exeModule;
    exeName.resize(length);
    exeName = getFilenameFromPath(exeName);

    exeModule = GetModule(exeName);
    return exeModule;
}

std::string LoadedModules::GetSelfDirectory()
{
    if (loadedModules.empty())
        LoadedModules::Refresh();

    if (modDirectory.empty())
    {
        std::string fullPath = LoadedModules::GetModule(MOD_NAME).first;

        size_t pos = fullPath.find_last_of("/\\");
        modDirectory = (pos != std::string::npos) ? fullPath.substr(0, pos) : "";
    }

    return modDirectory;
}

bool LoadedModules::IsModLoaded(loadedModNames mod)
{
    return loadedMods[mod];
}

void LoadedModules::Log()
{
    for (auto& i : loadedModules)
    {
        std::string hash = hashFile(i.first);
        Log::Write("0x%08X 0x%08X %s %s\n", i.second.lpBaseOfDll, i.second.SizeOfImage, hash.c_str(), i.first.c_str());
    }
}

void LoadedModules::Refresh()
{
    loadedModules.clear();

    HMODULE modules[500] = {};
    HANDLE hProcess = GetCurrentProcess();
    DWORD cbNeeded = 0;

    if (EnumProcessModules(hProcess, modules, sizeof(modules), &cbNeeded) && cbNeeded <= sizeof(modules))
        for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
        {
            char szModName[MAX_PATH] = {};
            if (GetModuleFileNameEx(hProcess, modules[i], szModName, sizeof(szModName) / sizeof(TCHAR)))
            {
                if (strcasestr(szModName, "III.VC.SA.LimitAdjuster"))
                    loadedMods[MOD_OLA] = true;
                else if (strcasestr(szModName, "fastman92limitAdjuster"))
                    loadedMods[MOD_FLA] = true;

#ifdef _DEBUG
                assert(!strcasecmp("ModelVariations.asi", getFilenameFromPath(szModName)));
#endif
                MODULEINFO mInfo;
                if (modules[i] && GetModuleInformation(hProcess, modules[i], &mInfo, sizeof(MODULEINFO)))
                    loadedModules.push_back({ szModName, mInfo });
            }
        }

    for (size_t i = 1; i < loadedModules.size(); ++i)
    {
        auto module = std::move(loadedModules[i]);
        size_t j = i;

        while (j && loadedModules[j - 1].second.lpBaseOfDll > module.second.lpBaseOfDll)
        {
            loadedModules[j] = std::move(loadedModules[j - 1]);
            --j;
        }

        loadedModules[j] = std::move(module);
    }
}
