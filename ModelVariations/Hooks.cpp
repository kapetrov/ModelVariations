#include "Helpers.hpp"
#include "Hooks.hpp"
#include "LoadedModules.hpp"
#include "Log.hpp"
#include "Memory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <Windows.h>

namespace {
    constexpr std::size_t MaxHookDescriptors = 512;
    constexpr std::size_t MaxSharedCallHooks = MaxHookDescriptors;
    constexpr std::size_t SharedCallThunkSize = 15;

    static_assert(sizeof(void*) == 4, "Shared-call thunks require an x86 build");

    std::array<hookinfo, MaxHookDescriptors> hookedCalls;
    std::size_t hookedCallCount = 0;
    std::array<asmhookinfo, MaxHookDescriptors> hooksASM;
    std::size_t asmHookCount = 0;
    std::array<SharedCallHookState, MaxSharedCallHooks> sharedCallStates;
    std::size_t sharedCallStateCount = 0;
    unsigned char* sharedCallThunkPool = nullptr;
    std::size_t sharedCallThunkCount = 0;

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

    bool ensureSharedCallThunkPool() noexcept
    {
        if (sharedCallThunkPool)
            return true;

        sharedCallThunkPool = static_cast<unsigned char*>(VirtualAlloc(nullptr, MaxSharedCallHooks * SharedCallThunkSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));

        if (!sharedCallThunkPool)
        {
            Log::Write("Error! Failed to allocate shared-call thunk memory (error %lu)\n", GetLastError());
            return false;
        }

        return true;
    }
}

SharedCallHookState* currentSharedCallHook = nullptr;

void logMissingOriginalFunction(std::uintptr_t address)
{
    Log::Write("Error! Original function not found for address 0x%08X\n", address);
}

void logMissingOriginalMethod(std::uintptr_t address)
{
    Log::Write("Error! Original method not found for address 0x%08X\n", address);
}

std::span<const hookinfo> getHookedCalls() noexcept
{
    return { hookedCalls.data(), hookedCallCount };
}

std::span<const asmhookinfo> getASMHooks() noexcept
{
    return { hooksASM.data(), asmHookCount };
}

std::size_t getSharedCallStateCount()
{
    return sharedCallStateCount;
}

std::size_t getNumMaxHooks()
{
    return MaxHookDescriptors;
}

__declspec(noinline) void* __fastcall createSharedCallThunkImpl(SharedCallHookState* state, void* target) noexcept
{
    if (!state || !target)
    {
        Log::Write("Error! Invalid shared-call thunk request\n");
        return nullptr;
    }

    if (sharedCallThunkCount >= MaxSharedCallHooks)
    {
        Log::Write("Error! Shared-call thunk capacity exceeded at address 0x%08X\n", state->address);
        return nullptr;
    }

    if (!ensureSharedCallThunkPool())
        return nullptr;

    unsigned char* const thunk = sharedCallThunkPool + sharedCallThunkCount * SharedCallThunkSize;

    // mov dword ptr [currentSharedCallHook], state
    thunk[0] = 0xC7;
    thunk[1] = 0x05;
    const std::uint32_t currentHookAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&currentSharedCallHook));
    const std::uint32_t stateAddress = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(state));
    std::memcpy(thunk + 2, &currentHookAddress, sizeof(currentHookAddress));
    std::memcpy(thunk + 6, &stateAddress, sizeof(stateAddress));

    // jmp target
    thunk[10] = 0xE9;
    const std::uintptr_t nextInstruction = reinterpret_cast<std::uintptr_t>(thunk + SharedCallThunkSize);
    const std::uint32_t relativeTarget = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target) - nextInstruction);
    std::memcpy(thunk + 11, &relativeTarget, sizeof(relativeTarget));

    ++sharedCallThunkCount;
    FlushInstructionCache(GetCurrentProcess(), thunk, SharedCallThunkSize);
    return thunk;
}

__declspec(noinline) SharedCallHookState* __fastcall hookSharedCallImpl(std::uintptr_t address, void* target, const char* name, bool isVTableAddress)
{
    if (sharedCallStateCount >= MaxSharedCallHooks)
    {
        Log::Write("Error! Shared-call hook capacity exceeded at address 0x%08X\n", address);
        return nullptr;
    }

    SharedCallHookState& state = sharedCallStates[sharedCallStateCount];
    state = { address, nullptr };

    void* const thunk = createSharedCallThunkImpl(&state, target);
    if (!thunk)
        return nullptr;

    ++sharedCallStateCount;

    if (void* originalFunction = hookCallImpl(address, thunk, name, isVTableAddress))
        state.originalFunction = originalFunction;

    return &state;
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
    const char* funcType = (strstr(funcName, "::") != nullptr) ? "Modified method" : "Modified function";

    if (branchDestination)
        Log::LogModifiedAddress(address, "%s detected: %s - 0x%08X is %s %s 0x%08X\n", funcType, funcName, address, bytes.c_str(), getFilenameFromPath(moduleName).c_str(), branchDestination);
    else
        Log::LogModifiedAddress(address, "%s detected: %s - 0x%08X is %s\n", funcType, funcName, address, bytes.c_str());

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
    Log::Write("Added call hook %s<0x%X>\n", name ? name : "UnknownHook", address);

    return originalAddress;
}
