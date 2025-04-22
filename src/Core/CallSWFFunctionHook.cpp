#include "CallSWFFunctionHook.h"
#include "../Util/Logger.h"
#include "../SWF/Dispatcher/Dispatcher.h"  // Include the dispatcher
#include "../SWF/Data/SWFArgument.h" // Include data types
#include "../SWF/Data/SWFReturn.h"   // Include data types
#include "../SWF/Helpers/SWFReturnHelper.h" // Include helpers

namespace HookCrashers {
    namespace Core {

        // --- Module Internals ---
        static Util::Logger& L = Util::Logger::Instance();

        using OriginalCallFunc_t = void(__thiscall*)(
            void* thisPtr,
            int swfContext,             // param_1
            uint32_t functionIdRaw,     // param_2
            int paramCount,             // param_3
            SWF::Data::SWFArgument** swfArgs, // param_4 - Assuming it's passed as array of pointers
            uint32_t* swfReturnRaw,     // param_5 - Raw pointer for return value
            uint32_t callbackPtr        // param_6
            );
        static OriginalCallFunc_t g_originalFunction = nullptr;

        // The address of the function we are hooking (FUN_00bfa070)
        constexpr uintptr_t CALL_SWF_FUNCTION_OFFSET = 0x7A070; // Verify this offset

        // --- Hook Function ---
        // This function replaces the game's main SWF call dispatcher.
        void __fastcall DetouredCallSWFFunction(
            void* thisPtr,              // Passed in ECX for __thiscall, __fastcall gets it as first arg
            void* /* edx - unused */,   // Dummy for __fastcall convention
            int swfContext,             // Stack param 1
            uint32_t functionIdRaw,     // Stack param 2
            int paramCount,             // Stack param 3
            SWF::Data::SWFArgument** swfArgs, // Stack param 4
            uint32_t* swfReturnRaw,     // Stack param 5
            uint32_t callbackPtr)       // Stack param 6
        {
            // Delegate the decision-making to the SwfDispatcher
            bool handled = SWF::Dispatcher::Dispatch(
                thisPtr, swfContext, functionIdRaw, paramCount,
                swfArgs, swfReturnRaw, callbackPtr
            );
            // If the dispatcher did NOT handle the call (returned false),
            // then we must call the original game function.
            if (!handled) {
                if (g_originalFunction) {
                    try {
                        // Call original using the stored function pointer
                        g_originalFunction(thisPtr, swfContext, functionIdRaw, paramCount, swfArgs, swfReturnRaw, callbackPtr);
                    }
                    catch (...) {
                        L.Get()->critical("!!! Exception in original CallSWFFunction !!!");
                        // Try to set failure if possible
                        if (swfReturnRaw) SWF::Helpers::SWFReturnHelper::SetFailure(swfReturnRaw);
                    }
                }
                else {
                    L.Get()->error("Original CallSWFFunction pointer is null! Cannot call original for ID {:#x}.", functionIdRaw & 0xFFFF);
                    if (swfReturnRaw) SWF::Helpers::SWFReturnHelper::SetFailure(swfReturnRaw);
                }
            }
            L.Get()->flush();
            // If handled is true, the dispatcher (or custom/override func) took care of everything.
        }

        // --- Public API ---
        bool SetupCallSWFFunctionHook(uintptr_t moduleBase) {
            L.Get()->info("Setting up CallSWFFunction hook...");
            uintptr_t targetAddress = moduleBase + CALL_SWF_FUNCTION_OFFSET;
            g_originalFunction = reinterpret_cast<OriginalCallFunc_t>(targetAddress);

            // Initialize the dispatcher with the original function address *before* hooking
            SWF::Dispatcher::Initialize(targetAddress);


            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            // Note: We are hooking a __thiscall function using a __fastcall detour.
            // Detours should handle the calling convention adjustment (ECX for thisPtr).
            LONG error = DetourAttach(&(PVOID&)g_originalFunction, DetouredCallSWFFunction);
            if (error != NO_ERROR) {
                L.Get()->error("DetourAttach failed for CallSWFFunction: {}", error);
                DetourTransactionAbort();
                g_originalFunction = nullptr; // Nullify on failure
                SWF::Dispatcher::Initialize(0); // Reset dispatcher pointer too
                return false;
            }
            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("DetourTransactionCommit failed for CallSWFFunction: {}", error);
                // Consider detaching? See previous hook setup.
                return false; // Indicate commit failure
            }

            L.Get()->info("CallSWFFunction hook attached successfully at 0x{:X}", targetAddress);
            L.Get()->flush();
            return true;
        }

        uintptr_t GetOriginalCallSWFFunctionAddress() {
            return reinterpret_cast<uintptr_t>(g_originalFunction);
        }
    }
}