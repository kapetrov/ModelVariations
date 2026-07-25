#include "Helpers.hpp"
#include "Hooks.hpp"
#include "LoadedModules.hpp"
#include "Log.hpp"
#include "Memory.hpp"

#include <string>
#include <unordered_map>

std::unordered_map<std::uintptr_t, const char*> hooksASM;
std::unordered_map<std::uintptr_t, hookinfo> hookedCalls;

bool hookASM(std::uintptr_t address, const char* originalData, injector::memory_pointer_raw hookDest, const char* funcName)
{
    unsigned numBytes = strlen(originalData) / 3 + 1;

    if (!memcmp(address, originalData) && forceEnableGlobal == false && !forceEnable.contains(address))
    {
        std::string bytes = bytesToString(address, numBytes);
        auto branchDestination = injector::GetBranchDestination(address).as_int();
        std::string moduleName = LoadedModules::GetModuleAtAddress(branchDestination).first;
        std::string funcType = (strstr(funcName, "::") != nullptr) ? "Modified method" : "Modified function";

        if (branchDestination)
            Log::LogModifiedAddress(address, "%s detected: %s - 0x%08X is %s %s 0x%08X\n", funcType.c_str(), funcName, address, bytes.c_str(), getFilenameFromPath(moduleName).c_str(), branchDestination);
        else
            Log::LogModifiedAddress(address, "%s detected: %s - 0x%08X is %s\n", funcType.c_str(), funcName, address, bytes.c_str());

        return false;
    }

    injector::MakeJMP(address, hookDest);

    hooksASM[address] = funcName;

    return true;
}

void hookCall(std::uintptr_t address, void* pFunction, const char *name, bool isVTableAddress)
{
    void* originalAddress;
    if (isVTableAddress)
    {
        originalAddress = *reinterpret_cast<void**>(address);
        *reinterpret_cast<void**>(address) = pFunction;
        hookedCalls.insert({ address, {name, originalAddress, pFunction, isVTableAddress} });
    }
    else
    {
        if (isAddressValid(injector::GetBranchDestination(address).as_int()))
        {
            originalAddress = reinterpret_cast<void*>(injector::MakeCALL(address, pFunction).as_int());
            hookedCalls.insert({ address, {name, originalAddress, pFunction, isVTableAddress} });
        }
        else
            Log::LogModifiedAddress(address, "Modified function call detected: %s - 0x%08X is %s\n", name, address, bytesToString(address, 5).c_str());
    }
}
