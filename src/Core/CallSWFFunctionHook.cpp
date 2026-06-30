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
            bool handled = SWF::Dispatcher::Dispatch(
                thisPtr, swfContext, functionIdRaw, paramCount,
                swfArgs, swfReturnRaw, callbackPtr);

            if (!handled) {
                if (g_originalFunction) {
                    try {
                        g_originalFunction(thisPtr, swfContext, functionIdRaw, paramCount, swfArgs, swfReturnRaw, callbackPtr);
                    }
                    catch (...) {
                        if (swfReturnRaw) SWF::Helpers::SWFReturnHelper::SetFailure(SWF::Helpers::SWFReturnHelper::AsStructured(swfReturnRaw));
                    }
                }
                else {
                    if (swfReturnRaw) SWF::Helpers::SWFReturnHelper::SetFailure(SWF::Helpers::SWFReturnHelper::AsStructured(swfReturnRaw));
                }
            }
        }

        bool SetupCallSWFFunctionHook(uintptr_t moduleBase) {
            uintptr_t targetAddress = moduleBase + CALL_SWF_FUNCTION_OFFSET;
            L.Get()->debug("[Hook] Installing hook | name=CallSWFFunction | RVA=0x{:X} | VA=0x{:X}.", CALL_SWF_FUNCTION_OFFSET, targetAddress);

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
                L.Get()->error("[Hook] DetourAttach failed | name=CallSWFFunction | RVA=0x{:X} | error={}", CALL_SWF_FUNCTION_OFFSET, error);
                DetourTransactionAbort();
                // Reset everything on failure
                g_originalFunction = nullptr;
                SWF::Dispatcher::Initialize(0);
                return false;
            }
            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("[Hook] DetourTransactionCommit failed | name=CallSWFFunction | RVA=0x{:X} | error={}", CALL_SWF_FUNCTION_OFFSET, error);
                return false;
            }

            L.Get()->debug("[Hook] Hook installed | name=CallSWFFunction | RVA=0x{:X} | VA=0x{:X}.", CALL_SWF_FUNCTION_OFFSET, targetAddress);
            return true;
        }

        uintptr_t GetOriginalCallSWFFunctionAddress() {
            return reinterpret_cast<uintptr_t>(g_originalFunction);
        }
    }
}
