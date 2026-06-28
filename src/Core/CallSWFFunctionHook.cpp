#include "CallSWFFunctionHook.h"
#include "../Util/Logger.h"
#include "../SWF/Dispatcher/Dispatcher.h"
#include "../SWF/Helpers/SWFReturnHelper.h"

// Define our aliases for the public types, which all our systems now use
using HC_SWFArgument = HookCrashers::SWF::Data::SWFArgument;
using HC_SWFReturn = HookCrashers::SWF::Data::SWFReturn;

namespace HookCrashers {
    namespace Core {

        static Util::Logger& L = Util::Logger::Instance();

        // FIXED: Updated function pointer type to use the public argument type
        using OriginalCallFunc_t = void(__thiscall*)(
            void* thisPtr,
            int swfContext,
            uint32_t functionIdRaw,
            int paramCount,
            HC_SWFArgument** swfArgs, // <-- FIXED
            uint32_t* swfReturnRaw,
            uint32_t callbackPtr
            );
        static OriginalCallFunc_t g_originalFunction = nullptr;

        constexpr uintptr_t CALL_SWF_FUNCTION_OFFSET = 0x11B230; // updated

        // FIXED: Updated detour function signature to match the game's actual function
        void __fastcall DetouredCallSWFFunction(
            void* thisPtr,
            void* /* edx - unused */,
            int swfContext,
            uint32_t functionIdRaw,
            int paramCount,
            HC_SWFArgument** swfArgs,
            uint32_t* swfReturnRaw,
            uint32_t callbackPtr)
        {
            static int s_callCount = 0;
            ++s_callCount;
            const bool logThisCall = s_callCount <= 80 || (s_callCount % 250) == 0;
            if (logThisCall) {
                L.Get()->info(
                    "[HookHit] CallSWFFunction ENTER call={} this=0x{:X} ctx=0x{:X} function_raw=0x{:X} function_id={} param_count={} args=0x{:X} ret=0x{:X} cb=0x{:X}.",
                    s_callCount,
                    reinterpret_cast<uintptr_t>(thisPtr),
                    static_cast<uint32_t>(swfContext),
                    functionIdRaw,
                    functionIdRaw & 0xFFFF,
                    paramCount,
                    reinterpret_cast<uintptr_t>(swfArgs),
                    reinterpret_cast<uintptr_t>(swfReturnRaw),
                    callbackPtr);
                L.Get()->flush();
            }

            bool handled = SWF::Dispatcher::Dispatch(
                thisPtr, swfContext, functionIdRaw, paramCount,
                swfArgs, swfReturnRaw, callbackPtr);

            if (logThisCall) {
                L.Get()->info("[HookHit] CallSWFFunction dispatch handled={} call={}", handled, s_callCount);
                L.Get()->flush();
            }

            if (!handled) {
                if (g_originalFunction) {
                    try {
                        g_originalFunction(thisPtr, swfContext, functionIdRaw, paramCount, swfArgs, swfReturnRaw, callbackPtr);
                        if (logThisCall) {
                            L.Get()->info("[HookHit] CallSWFFunction LEAVE original call={}", s_callCount);
                            L.Get()->flush();
                        }
                    }
                    catch (...) {
                        L.Get()->critical("[HookHit] CallSWFFunction original threw unknown exception call={} function_id={}", s_callCount, functionIdRaw & 0xFFFF);
                        L.Get()->flush();
                        if (swfReturnRaw) SWF::Helpers::SWFReturnHelper::SetFailure(SWF::Helpers::SWFReturnHelper::AsStructured(swfReturnRaw));
                    }
                }
                else {
                    L.Get()->error("[HookHit] CallSWFFunction original pointer is null for function_id={}", functionIdRaw & 0xFFFF);
                    L.Get()->flush();
                    if (swfReturnRaw) SWF::Helpers::SWFReturnHelper::SetFailure(SWF::Helpers::SWFReturnHelper::AsStructured(swfReturnRaw));
                }
            }
        }

        bool SetupCallSWFFunctionHook(uintptr_t moduleBase) {
            uintptr_t targetAddress = moduleBase + CALL_SWF_FUNCTION_OFFSET;
            L.Get()->info("[Hook] Attaching CallSWFFunction hook at offset 0x{:X} (address=0x{:X}).", CALL_SWF_FUNCTION_OFFSET, targetAddress);

            // Initialize the dispatcher with the original address before we hook it
            SWF::Dispatcher::Initialize(targetAddress);

            // This needs to be done *after* we store the original address for the dispatcher
            // But before we attach, we set g_originalFunction to the same address.
            // This is slightly redundant but safe.
            g_originalFunction = reinterpret_cast<OriginalCallFunc_t>(targetAddress);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            // DetourAttach wants a PVOID&, so we cast our function pointer variable's address
            LONG error = DetourAttach(&(PVOID&)g_originalFunction, DetouredCallSWFFunction);
            if (error != NO_ERROR) {
                L.Get()->error("[Hook] CallSWFFunction DetourAttach failed at offset 0x{:X}: {}", CALL_SWF_FUNCTION_OFFSET, error);
                DetourTransactionAbort();
                // Reset everything on failure
                g_originalFunction = nullptr;
                SWF::Dispatcher::Initialize(0);
                return false;
            }
            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("[Hook] CallSWFFunction DetourTransactionCommit failed at offset 0x{:X}: {}", CALL_SWF_FUNCTION_OFFSET, error);
                return false;
            }

            L.Get()->info("[Hook] CallSWFFunction hook attached successfully at offset 0x{:X} (address=0x{:X}).", CALL_SWF_FUNCTION_OFFSET, targetAddress);
            return true;
        }

        uintptr_t GetOriginalCallSWFFunctionAddress() {
            return reinterpret_cast<uintptr_t>(g_originalFunction);
        }
    }
}
