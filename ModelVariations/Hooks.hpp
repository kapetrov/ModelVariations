#pragma once

#include <cstddef>
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

bool hookASM(std::uintptr_t address, std::size_t numberOfBytes, injector::memory_pointer_raw hookDest, const char* funcName);
void* hookCallImpl(std::uintptr_t address, void* pFunction, const char* name, bool isVTableAddress);
void logMissingOriginalFunction(std::uintptr_t address);
void logMissingOriginalMethod(std::uintptr_t address);

std::span<const hookinfo> getHookedCalls() noexcept;
std::span<const asmhookinfo> getASMHooks() noexcept;

template <std::uintptr_t address>
struct OriginalHookSlot {
    static inline void* function = nullptr;
};

struct SharedCallHookState {
    std::uintptr_t address;
    void* originalFunction;
};

extern SharedCallHookState* currentSharedCallHook;
__declspec(noinline) SharedCallHookState* __fastcall hookSharedCallImpl(std::uintptr_t address, void* target, const char* name, bool isVTableAddress);
__declspec(noinline) void* __fastcall createSharedCallThunkImpl(SharedCallHookState* state, void* target) noexcept;

struct CapturedOriginalCall {
    std::uintptr_t address;
    void* function;

    template <typename... Args>
    void call(Args... args) const
    {
        if (function)
            reinterpret_cast<void(__cdecl*)(Args...)>(function)(args...);
        else
            logMissingOriginalFunction(address);
    }

    template <typename Ret, typename... Args>
    Ret callAndReturn(Args... args) const
    {
        if (function)
            return reinterpret_cast<Ret(__cdecl*)(Args...)>(function)(args...);

        logMissingOriginalFunction(address);
        return Ret{};
    }

    template <typename C, typename... Args>
    void callMethod(C _this, Args... args) const
    {
        if (function)
            reinterpret_cast<void(__thiscall*)(C, Args...)>(function)(_this, args...);
        else
            logMissingOriginalMethod(address);
    }

    template <typename Ret, typename C, typename... Args>
    Ret callMethodAndReturn(C _this, Args... args) const
    {
        if (function)
            return reinterpret_cast<Ret(__thiscall*)(C, Args...)>(function)(_this, args...);

        logMissingOriginalMethod(address);
        return Ret{};
    }
};

inline CapturedOriginalCall captureCurrentOriginalCall() noexcept
{
    if (currentSharedCallHook)
        return { currentSharedCallHook->address, currentSharedCallHook->originalFunction };

    return {};
}

template <std::uintptr_t address, auto Target>
__forceinline SharedCallHookState* hookSharedCall(const char* name, bool isVTableAddress = false)
{
    using TargetType = decltype(Target);
    static_assert(std::is_pointer_v<TargetType> && std::is_function_v<std::remove_pointer_t<TargetType>>, "Hook destination must be a function pointer");

    return hookSharedCallImpl(address, reinterpret_cast<void*>(Target), name, isVTableAddress);
}

template <auto Target>
__forceinline decltype(Target) createSharedCallThunk(SharedCallHookState& state) noexcept
{
    using TargetType = decltype(Target);
    static_assert(std::is_pointer_v<TargetType> && std::is_function_v<std::remove_pointer_t<TargetType>>, "Thunk destination must be a function pointer");

    return reinterpret_cast<TargetType>(createSharedCallThunkImpl(&state, reinterpret_cast<void*>(Target)));
}

template <std::uintptr_t address, typename Function>
void hookCall(Function pFunction, const char* name, bool isVTableAddress = false)
{
    static_assert(std::is_pointer_v<Function>, "Hook destination must be a function pointer");

    void* changedFunction = reinterpret_cast<void*>(pFunction);
    if (void* originalFunction = hookCallImpl(address, changedFunction, name, isVTableAddress))
        OriginalHookSlot<address>::function = originalFunction;
}
