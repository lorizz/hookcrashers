#pragma once
#include <type_traits>
#include <stdexcept>

namespace HookCrashers {
    namespace Native {
        enum class CallingConvention {
            StdCall,
            FastCall,
            ThisCall,
            Cdecl
        };

        template <typename T>
        struct NativeInfo {
            T Address;
            CallingConvention Convention;

            uintptr_t GetAddressValue() const { return reinterpret_cast<uintptr_t>(Address); }

            NativeInfo(T addr = nullptr, CallingConvention conv = CallingConvention::StdCall)
                : Address(addr), Convention(conv) {
            }

            bool IsValid() const { return Address != nullptr; }
        };

        namespace Internal {
            template <typename Ret, typename... Args>
            inline Ret StdCallFunc(void* func, Args... args) {
                typedef Ret(__stdcall* FunctionType)(Args...);
                return reinterpret_cast<FunctionType>(func)(args...);
            }

            template <typename Ret, typename... Args>
            inline Ret CdeclFunc(void* func, Args... args) {
                typedef Ret(__cdecl* FunctionType)(Args...);
                return reinterpret_cast<FunctionType>(func)(args...);
            }

            template <typename Ret, typename... Args>
            inline Ret FastCallFunc(void* func, Args... args) {
                typedef Ret(__fastcall* FunctionType)(Args...);
                return reinterpret_cast<FunctionType>(func)(args...);
            }

            template <typename Ret, typename... Args>
            inline Ret ThisCallFunc(void* func, Args... args) {
                typedef Ret(__thiscall* FunctionType)(Args...);
                return reinterpret_cast<FunctionType>(func)(args...);
            }
        }

        // Template principale per chiamare i nativi
        template <typename Ret = void, typename... Args>
        inline Ret CallNative(const NativeInfo<void*>& native, Args... args) {
            if (!native.IsValid()) {
                throw std::runtime_error("Attempted to call an uninitialized native function.");
            }

            switch (native.Convention) {
            case CallingConvention::StdCall:
                return Internal::StdCallFunc<Ret, Args...>(native.Address, args...);
            case CallingConvention::Cdecl:
                return Internal::CdeclFunc<Ret, Args...>(native.Address, args...);
            case CallingConvention::FastCall:
                return Internal::FastCallFunc<Ret, Args...>(native.Address, args...);
            case CallingConvention::ThisCall:
                return Internal::ThisCallFunc<Ret, Args...>(native.Address, args...);
            default:
                throw std::runtime_error("Unsupported calling convention in CallNative.");
            }
        }
    }
}