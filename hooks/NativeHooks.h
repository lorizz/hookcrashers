#pragma once
#include <type_traits>
#include <stdexcept>

namespace NativeHooks
{
    enum class CallingConvention
    {
        StdCall,
        FastCall,
        ThisCall
    };

    template <typename T>
    struct NativeInfo
    {
        T Address;
        CallingConvention Convention;
        NativeInfo(T addr, CallingConvention conv = CallingConvention::StdCall)
            : Address(addr), Convention(conv) {}
    };

    // Funzioni wrapper per calling conventions
    template <typename Ret, typename... Args>
    inline Ret __stdcall StdCallFunction(void* func, Args... args)
    {
        typedef Ret(__stdcall* FunctionType)(Args...);
        return ((FunctionType)func)(args...);
    }

    template <typename Ret, typename... Args>
    inline Ret __fastcall FastCallFunction(void* func, Args... args)
    {
        typedef Ret(__fastcall* FunctionType)(Args...);
        return ((FunctionType)func)(args...);
    }

    // Caso speciale per FastCall senza parametri
    template <typename Ret>
    inline Ret FastCallNoParams(void* func)
    {
        typedef Ret(__fastcall* FunctionType)();
        return ((FunctionType)func)();
    }

    // __thiscall con un unico parametro (thisPtr)
    template <typename Ret>
    inline Ret ThisCallNoParams(void* func, void* thisPtr)
    {
        typedef Ret(__fastcall* ThisCallType)(void*, void*);
        return ((ThisCallType)func)(thisPtr, nullptr);
    }

    // __thiscall con thisPtr e un parametro
    template <typename Ret, typename Arg1>
    inline Ret ThisCall1Param(void* func, void* thisPtr, Arg1 arg1)
    {
        typedef Ret(__fastcall* ThisCallType)(void*, void*, Arg1);
        return ((ThisCallType)func)(thisPtr, nullptr, arg1);
    }

    // __thiscall con thisPtr e due parametri
    template <typename Ret, typename Arg1, typename Arg2>
    inline Ret ThisCall2Params(void* func, void* thisPtr, Arg1 arg1, Arg2 arg2)
    {
        typedef Ret(__fastcall* ThisCallType)(void*, void*, Arg1, Arg2);
        return ((ThisCallType)func)(thisPtr, nullptr, arg1, arg2);
    }

    // __thiscall con thisPtr e tre parametri
    template <typename Ret, typename Arg1, typename Arg2, typename Arg3>
    inline Ret ThisCall3Params(void* func, void* thisPtr, Arg1 arg1, Arg2 arg2, Arg3 arg3)
    {
        typedef Ret(__fastcall* ThisCallType)(void*, void*, Arg1, Arg2, Arg3);
        return ((ThisCallType)func)(thisPtr, nullptr, arg1, arg2, arg3);
    }

    // CallNative principale
    template <typename Ret = void, typename... Args>
    inline typename std::enable_if<sizeof...(Args) == 0, Ret>::type
        CallNative(const NativeInfo<void*>& native)
    {
        switch (native.Convention)
        {
        case CallingConvention::StdCall:
            return StdCallFunction<Ret>(native.Address);
        case CallingConvention::FastCall:
            return FastCallNoParams<Ret>(native.Address);
        case CallingConvention::ThisCall:
            throw std::runtime_error("ThisCall requires at least the 'this' pointer");
        default:
            return StdCallFunction<Ret>(native.Address);
        }
    }

    // Specializzazione per un parametro
    template <typename Ret = void, typename Arg1>
    inline Ret CallNative(const NativeInfo<void*>& native, Arg1 arg1)
    {
        switch (native.Convention)
        {
        case CallingConvention::StdCall:
            return StdCallFunction<Ret, Arg1>(native.Address, arg1);
        case CallingConvention::FastCall:
            return FastCallFunction<Ret, Arg1>(native.Address, arg1);
        case CallingConvention::ThisCall:
            return ThisCallNoParams<Ret>(native.Address, reinterpret_cast<void*>(arg1));
        default:
            return StdCallFunction<Ret, Arg1>(native.Address, arg1);
        }
    }

    // Specializzazione per due parametri
    template <typename Ret = void, typename Arg1, typename Arg2>
    inline Ret CallNative(const NativeInfo<void*>& native, Arg1 arg1, Arg2 arg2)
    {
        switch (native.Convention)
        {
        case CallingConvention::StdCall:
            return StdCallFunction<Ret, Arg1, Arg2>(native.Address, arg1, arg2);
        case CallingConvention::FastCall:
            return FastCallFunction<Ret, Arg1, Arg2>(native.Address, arg1, arg2);
        case CallingConvention::ThisCall:
            return ThisCall1Param<Ret, Arg2>(native.Address, reinterpret_cast<void*>(arg1), arg2);
        default:
            return StdCallFunction<Ret, Arg1, Arg2>(native.Address, arg1, arg2);
        }
    }

    // Specializzazione per tre parametri
    template <typename Ret = void, typename Arg1, typename Arg2, typename Arg3>
    inline Ret CallNative(const NativeInfo<void*>& native, Arg1 arg1, Arg2 arg2, Arg3 arg3)
    {
        switch (native.Convention)
        {
        case CallingConvention::StdCall:
            return StdCallFunction<Ret, Arg1, Arg2, Arg3>(native.Address, arg1, arg2, arg3);
        case CallingConvention::FastCall:
            return FastCallFunction<Ret, Arg1, Arg2, Arg3>(native.Address, arg1, arg2, arg3);
        case CallingConvention::ThisCall:
            return ThisCall2Params<Ret, Arg2, Arg3>(native.Address, reinterpret_cast<void*>(arg1), arg2, arg3);
        default:
            return StdCallFunction<Ret, Arg1, Arg2, Arg3>(native.Address, arg1, arg2, arg3);
        }
    }

    // Specializzazione per quattro parametri
    template <typename Ret = void, typename Arg1, typename Arg2, typename Arg3, typename Arg4>
    inline Ret CallNative(const NativeInfo<void*>& native, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4)
    {
        switch (native.Convention)
        {
        case CallingConvention::StdCall:
            return StdCallFunction<Ret, Arg1, Arg2, Arg3, Arg4>(native.Address, arg1, arg2, arg3, arg4);
        case CallingConvention::FastCall:
            return FastCallFunction<Ret, Arg1, Arg2, Arg3, Arg4>(native.Address, arg1, arg2, arg3, arg4);
        case CallingConvention::ThisCall:
            return ThisCall3Params<Ret, Arg2, Arg3, Arg4>(native.Address, reinterpret_cast<void*>(arg1), arg2, arg3, arg4);
        default:
            return StdCallFunction<Ret, Arg1, Arg2, Arg3, Arg4>(native.Address, arg1, arg2, arg3, arg4);
        }
    }
}

struct Natives
{
    static NativeHooks::NativeInfo<void*> GetFirstArgument;
    static NativeHooks::NativeInfo<void*> GetNextArgument;
    static NativeHooks::NativeInfo<void*> SetStorageOffset;
    static NativeHooks::NativeInfo<void*> ReadStorageData;
};

void LoadNatives(uintptr_t moduleBase);
