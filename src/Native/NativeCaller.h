#pragma once

/*#include "../stdafx.h"
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
				: Address(addr), Convention(conv) {}

			bool IsValid() const { return Address != nullptr; }

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


                // Fastcall passes first two args (or one if fits) in ECX, EDX
                // Note: MSVC __fastcall implementation details can vary slightly.
                // These templates assume standard MSVC __fastcall behavior.

                // 0 args
                template <typename Ret>
                inline Ret FastCallFunc(void* func) {
                    typedef Ret(__fastcall* FunctionType)();
                    return reinterpret_cast<FunctionType>(func)();
                }
                // 1 arg
                template <typename Ret, typename Arg1>
                inline Ret FastCallFunc(void* func, Arg1 arg1) {
                    typedef Ret(__fastcall* FunctionType)(Arg1);
                    return reinterpret_cast<FunctionType>(func)(arg1);
                }
                // 2 args
                template <typename Ret, typename Arg1, typename Arg2>
                inline Ret FastCallFunc(void* func, Arg1 arg1, Arg2 arg2) {
                    typedef Ret(__fastcall* FunctionType)(Arg1, Arg2);
                    return reinterpret_cast<FunctionType>(func)(arg1, arg2);
                }
                // 3+ args (first two in registers, rest on stack)
                template <typename Ret, typename Arg1, typename Arg2, typename... RestArgs>
                inline Ret FastCallFunc(void* func, Arg1 arg1, Arg2 arg2, RestArgs... rest) {
                    typedef Ret(__fastcall* FunctionType)(Arg1, Arg2, RestArgs...);
                    return reinterpret_cast<FunctionType>(func)(arg1, arg2, rest...);
                }


                // Thiscall passes 'this' pointer in ECX, others on stack
                // Note: MSVC often implements thiscall similar to fastcall for member functions
                // where 'this' is passed implicitly in ECX. We'll use __thiscall keyword.

                // 0 params (only this)
                template<typename Ret, typename ThisPtr>
                inline Ret ThisCallFunc(void* func, ThisPtr pThis) {
                    typedef Ret(__thiscall* FuncType)(ThisPtr);
                    return reinterpret_cast<FuncType>(func)(pThis);
                }
                // 1 param
                template<typename Ret, typename ThisPtr, typename P1>
                inline Ret ThisCallFunc(void* func, ThisPtr pThis, P1 p1) {
                    typedef Ret(__thiscall* FuncType)(ThisPtr, P1);
                    return reinterpret_cast<FuncType>(func)(pThis, p1);
                }
                // 2 params
                template<typename Ret, typename ThisPtr, typename P1, typename P2>
                inline Ret ThisCallFunc(void* func, ThisPtr pThis, P1 p1, P2 p2) {
                    typedef Ret(__thiscall* FuncType)(ThisPtr, P1, P2);
                    return reinterpret_cast<FuncType>(func)(pThis, p1, p2);
                }
                // 3 params
                template<typename Ret, typename ThisPtr, typename P1, typename P2, typename P3>
                inline Ret ThisCallFunc(void* func, ThisPtr pThis, P1 p1, P2 p2, P3 p3) {
                    typedef Ret(__thiscall* FuncType)(ThisPtr, P1, P2, P3);
                    return reinterpret_cast<FuncType>(func)(pThis, p1, p2, p3);
                }
            }

            template <typename Ret = void, typename... Args>
            inline Ret CallNative(const NativeInfo<void*>& native, Args... args) {
                if (!native.IsValid()) {
                    // Maybe log here instead of throwing? Depends on desired behavior.
                    // Util::Logger::Instance().Get()->error("Attempted to call invalid native function!");
                    throw std::runtime_error("Attempted to call an uninitialized native function.");
                    // Or return default Ret(): return Ret{}; (requires Ret to be default constructible)
                }

                switch (native.Convention) {
                case CallingConvention::StdCall:
                    return Internal::StdCallFunc<Ret, Args...>(native.Address, args...);
                case CallingConvention::Cdecl:
                    return Internal::CdeclFunc<Ret, Args...>(native.Address, args...);
                case CallingConvention::FastCall:
                    // Fastcall template handles different arg counts internally now
                    return Internal::FastCallFunc<Ret, Args...>(native.Address, args...);
                case CallingConvention::ThisCall:
                    // ThisCall expects 'this' as the first argument from the caller
                    return Internal::ThisCallFunc<Ret, Args...>(native.Address, args...);
                default:
                    throw std::runtime_error("Unsupported calling convention in CallNative.");
                    // return Ret{};
                }
            }
		};
	}
}*/