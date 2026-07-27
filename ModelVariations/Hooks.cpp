#include "Helpers.hpp"
#include "Hooks.hpp"
#include "LoadedModules.hpp"
#include "Log.hpp"
#include "Memory.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <string>

namespace {
    constexpr std::size_t MaxHookDescriptors = 512;

    std::array<hookinfo, MaxHookDescriptors> hookedCalls;
    std::size_t hookedCallCount = 0;
    std::array<asmhookinfo, MaxHookDescriptors> hooksASM;
    std::size_t asmHookCount = 0;

    template <typename Descriptor, std::size_t Size>
    void storeHookDescriptor(std::array<Descriptor, Size>& descriptors, std::size_t& count, const Descriptor& descriptor)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            if (descriptors[i].address == descriptor.address)
            {
                descriptors[i] = descriptor;
                return;
            }
        }

        if (count < descriptors.size())
            descriptors[count++] = descriptor;
        else
            Log::Write("Error! Hook diagnostic descriptor capacity exceeded at address 0x%08X\n", descriptor.address);
    }
}

std::span<const hookinfo> getHookedCalls() noexcept
{
    return { hookedCalls.data(), hookedCallCount };
}

std::span<const asmhookinfo> getASMHooks() noexcept
{
    return { hooksASM.data(), asmHookCount };
}

void logMissingOriginalFunction(std::uintptr_t address)
{
    Log::Write("Error! Original function not found for address 0x%08X\n", address);
}

void logMissingOriginalMethod(std::uintptr_t address)
{
    Log::Write("Error! Original method not found for address 0x%08X\n", address);
}

bool hookASM(std::uintptr_t address, std::size_t numberOfBytes, injector::memory_pointer_raw hookDest, const char* funcName)
{
    if (memoryMatchesOriginalExe(address, numberOfBytes) || forceEnableGlobal || forceEnable.contains(address))
    {
        injector::MakeJMP(address, hookDest);
        storeHookDescriptor(hooksASM, asmHookCount, { address, funcName });
        return true;
    }
    
    std::string bytes = bytesToString(address, numberOfBytes);
    auto branchDestination = injector::GetBranchDestination(address).as_int();
    std::string moduleName = LoadedModules::GetModuleAtAddress(branchDestination).first;

    if (funcName && branchDestination)
    {
        const char* funcType = (strstr(funcName, "::") != nullptr) ? "Modified method" : "Modified function";
        Log::LogModifiedAddress(address, "%s detected: %s - 0x%08X is %s %s 0x%08X\n", funcType, funcName, address, bytes.c_str(), getFilenameFromPath(moduleName).c_str(), branchDestination);
    }
    else if (funcName)
    {
        const char* funcType = (strstr(funcName, "::") != nullptr) ? "Modified method" : "Modified function";
        Log::LogModifiedAddress(address, "%s detected: %s - 0x%08X is %s\n", funcType, funcName, address, bytes.c_str());
    }
    else if (branchDestination)
        Log::LogModifiedAddress(address, "Modified ASM hook detected: 0x%08X is %s %s 0x%08X\n", address, bytes.c_str(), getFilenameFromPath(moduleName).c_str(), branchDestination);
    else
        Log::LogModifiedAddress(address, "Modified ASM hook detected: 0x%08X is %s\n", address, bytes.c_str());

    return false;
}

void* hookCallImpl(std::uintptr_t address, void* pFunction, const char* name, bool isVTableAddress)
{
    void* originalAddress = nullptr;
    if (isVTableAddress)
    {
        originalAddress = *reinterpret_cast<void**>(address);
        *reinterpret_cast<void**>(address) = pFunction;
    }
    else
    {
        if (isAddressValid(injector::GetBranchDestination(address).as_int()))
            originalAddress = reinterpret_cast<void*>(injector::MakeCALL(address, pFunction).as_int());
        else
        {
            if (name)
                Log::LogModifiedAddress(address, "Modified function call detected: %s - 0x%08X is %s\n", name, address, bytesToString(address, 5).c_str());
            else
                Log::LogModifiedAddress(address, "Modified function call detected: 0x%08X is %s\n", address, bytesToString(address, 5).c_str());

            return nullptr;
        }
    }

    storeHookDescriptor(hookedCalls, hookedCallCount, { address, name, originalAddress, pFunction, isVTableAddress });

    return originalAddress;
}
