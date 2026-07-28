#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <type_traits>
#include <utility>


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

template <std::uintptr_t address>
struct SharedCallHookSlot {
    static inline SharedCallHookState state{ address, nullptr };
};

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

template <std::uintptr_t address, auto Target, typename Signature = decltype(Target)>
struct GeneratedCallThunk;

template <std::uintptr_t address, auto Target, typename Ret, typename... Args>
struct GeneratedCallThunk<address, Target, Ret(__cdecl*)(Args...)> {
    static Ret __cdecl invoke(Args... args)
    {
        currentSharedCallHook = &SharedCallHookSlot<address>::state;
        return Target(std::forward<Args>(args)...);
    }
};

template <std::uintptr_t address, auto Target, typename Ret, typename... Args>
struct GeneratedCallThunk<address, Target, Ret(__fastcall*)(Args...)> {
    static Ret __fastcall invoke(Args... args)
    {
        currentSharedCallHook = &SharedCallHookSlot<address>::state;
        return Target(std::forward<Args>(args)...);
    }
};

template <std::uintptr_t address, auto Target>
void hookSharedCall(const char* name, bool isVTableAddress = false)
{
    using Thunk = GeneratedCallThunk<address, Target>;

    void* changedFunction = reinterpret_cast<void*>(&Thunk::invoke);
    if (void* originalFunction = hookCallImpl(address, changedFunction, name, isVTableAddress))
        SharedCallHookSlot<address>::state.originalFunction = originalFunction;
}

template <std::uintptr_t address, typename Function>
void hookCall(Function pFunction, const char* name, bool isVTableAddress = false)
{
    static_assert(std::is_pointer_v<Function>, "Hook destination must be a function pointer");

    void* changedFunction = reinterpret_cast<void*>(pFunction);
    if (void* originalFunction = hookCallImpl(address, changedFunction, name, isVTableAddress))
        OriginalHookSlot<address>::function = originalFunction;
}
