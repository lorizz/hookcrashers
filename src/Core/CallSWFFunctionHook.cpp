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

        constexpr uintptr_t CALL_SWF_FUNCTION_OFFSET = 0x118710;

        // FIXED: Updated detour function signature to match the game's actual function
        void __fastcall DetouredCallSWFFunction(
            void* thisPtr,
            void* /* edx - unused */,
            int swfContext,
            uint32_t functionIdRaw,
            int paramCount,
            HC_SWFArgument** swfArgs, // <-- FIXED
            uint32_t* swfReturnRaw,
            uint32_t callbackPtr)
        {
            // The Dispatch function was already updated to expect these types
            bool handled = SWF::Dispatcher::Dispatch(
                thisPtr, swfContext, functionIdRaw, paramCount,
                swfArgs, swfReturnRaw, callbackPtr
            );
            if (!handled) {
                if (g_originalFunction) {
                    try {
                        // The original function call is fine as we are passing the arguments through
                        g_originalFunction(thisPtr, swfContext, functionIdRaw, paramCount, swfArgs, swfReturnRaw, callbackPtr);
                    }
                    catch (...) {
                        //L.Get()->critical("!!! Exception in original CallSWFFunction !!!");
                        // FIXED: Use the helper. It expects a structured pointer, so we cast.
                        if (swfReturnRaw) SWF::Helpers::SWFReturnHelper::SetFailure(SWF::Helpers::SWFReturnHelper::AsStructured(swfReturnRaw));
                    }
                }
                else {
                    //L.Get()->error("Original CallSWFFunction pointer is null! Cannot call original for ID {:#x}.", functionIdRaw & 0xFFFF);
                    // FIXED: Use the helper with the cast.
                    if (swfReturnRaw) SWF::Helpers::SWFReturnHelper::SetFailure(SWF::Helpers::SWFReturnHelper::AsStructured(swfReturnRaw));
                }
            }
            //L.Get()->flush();
        }

        bool SetupCallSWFFunctionHook(uintptr_t moduleBase) {
            //L.Get()->info("Setting up CallSWFFunction hook...");
            uintptr_t targetAddress = moduleBase + CALL_SWF_FUNCTION_OFFSET;

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
                //L.Get()->error("DetourAttach failed for CallSWFFunction: {}", error);
                DetourTransactionAbort();
                // Reset everything on failure
                g_originalFunction = nullptr;
                SWF::Dispatcher::Initialize(0);
                return false;
            }
            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                //L.Get()->error("DetourTransactionCommit failed for CallSWFFunction: {}", error);
                return false;
            }

            //L.Get()->info("CallSWFFunction hook attached successfully at 0x{:X}", targetAddress);
            //L.Get()->flush();
            return true;
        }

        uintptr_t GetOriginalCallSWFFunctionAddress() {
            return reinterpret_cast<uintptr_t>(g_originalFunction);
        }
    }
}