#pragma once

#include <cstdint>
#include <set>
#include <span>
#include <type_traits>


#include <injector/assembly.hpp>

struct hookinfo {
    std::uintptr_t address;
    const char* name;
    void* originalFunction;
    void* changedFunction;
    bool isVTableAddress;
};

struct asmhookinfo {
    std::uintptr_t address;
    const char* name;
};

extern bool forceEnableGlobal;
extern std::set<std::uintptr_t> forceEnable;

bool hookASM(std::uintptr_t address, const char* originalData, injector::memory_pointer_raw hookDest, const char* funcName);
void* hookCallImpl(std::uintptr_t address, void* pFunction, const char* name, bool isVTableAddress);
void logMissingOriginalFunction(std::uintptr_t address);
void logMissingOriginalMethod(std::uintptr_t address);

std::span<const hookinfo> getHookedCalls() noexcept;
std::span<const asmhookinfo> getASMHooks() noexcept;

template <std::uintptr_t address>
struct OriginalHookSlot {
    static inline void* function = nullptr;
};

template <std::uintptr_t address, typename Function>
void hookCall(Function pFunction, const char* name, bool isVTableAddress = false)
{
    static_assert(std::is_pointer_v<Function>, "Hook destination must be a function pointer");

    void* changedFunction = reinterpret_cast<void*>(pFunction);
    if (void* originalFunction = hookCallImpl(address, changedFunction, name, isVTableAddress))
        OriginalHookSlot<address>::function = originalFunction;
}

template <std::uintptr_t address, typename... Args>
void callOriginal(Args... args)
{
    if (void* originalFunction = OriginalHookSlot<address>::function)
        reinterpret_cast<void(__cdecl*)(Args...)>(originalFunction)(args...);
    else
        logMissingOriginalFunction(address);
}

template <typename Ret, std::uintptr_t address, typename... Args>
Ret callOriginalAndReturn(Args... args)
{
    if (void* originalFunction = OriginalHookSlot<address>::function)
        return reinterpret_cast<Ret(__cdecl*)(Args...)>(originalFunction)(args...);
    else
        logMissingOriginalFunction(address);

    return Ret{};
}


template <std::uintptr_t address, typename C, typename... Args>
void callMethodOriginal(C _this, Args... args)
{
    if (void* originalFunction = OriginalHookSlot<address>::function)
        reinterpret_cast<void(__thiscall*)(C, Args...)>(originalFunction)(_this, args...);
    else
        logMissingOriginalMethod(address);
}

template <typename Ret, std::uintptr_t address, typename C, typename... Args>
Ret callMethodOriginalAndReturn(C _this, Args... args)
{
    if (void* originalFunction = OriginalHookSlot<address>::function)
        return reinterpret_cast<Ret(__thiscall*)(C, Args...)>(originalFunction)(_this, args...);
    else
        logMissingOriginalMethod(address);

    return Ret{};
}
