#include "RegisterAllSWFFunctionsHook.h"
#include "../Util/Logger.h"
#include "../SWF/Custom/CustomFunctions.h" // To call RegisterAllWithGame
#include "RegisterSWFFunctionHook.h" // To get captured thisPtr and original function address

namespace HookCrashers {
    namespace Core {
        static Util::Logger& L = Util::Logger::Instance();

        using OriginalRegisterAll_t = void(__fastcall*)(void* param_1); // Use void* for simplicity
        static OriginalRegisterAll_t g_originalFunction = nullptr;

        // The address of the function we are hooking (FUN_00c002a0)
        constexpr uintptr_t REGISTER_ALL_SWF_FUNCTIONS_OFFSET = 0x802A0; // Verify this offset

        // --- Hook Function ---
        void __fastcall DetouredRegisterAllSWFFunctions(void* param_1) {
            L.Get()->info("RegisterAllSWFFunctions hook executing...");

            // 1. Call the original function first to let the game register its standard functions
            if (g_originalFunction) {
                L.Get()->debug("Calling original RegisterAllSWFFunctions...");
                try {
                    g_originalFunction(param_1);
                    L.Get()->debug("Original RegisterAllSWFFunctions finished.");
                }
                catch (...) {
                    L.Get()->critical("!!! Exception in original RegisterAllSWFFunctions !!!");
                    // Game state might be bad, proceed with caution
                }
            }
            else {
                L.Get()->error("Original RegisterAllSWFFunctions pointer is null! Cannot call original.");
                // If the original can't be called, the game might not function correctly.
                // Should we abort? For now, continue to register custom functions.
            }

            // 2. Register our custom functions with the game engine
            L.Get()->debug("Attempting to register custom SWF functions with the game...");
            void* capturedThisPtr = Core::GetCorrectThisPtr(); // Get the 'this' captured by the other hook

            if (capturedThisPtr) {
                SWF::Custom::RegisterAllWithGame(capturedThisPtr, 0);
                //uint16_t helloWorldId = SWF::Data::ToValue(SWF::Data::SWFFunctionID::HelloWorld);
                //DetouredRegisterSWFFunction(capturedThisPtr, nullptr, helloWorldId, "HelloWorld");
            }
            else {
                L.Get()->error("Could not register custom functions: Captured 'this' pointer is NULL.");
            }

            L.Get()->info("RegisterAllSWFFunctions hook finished.");
            L.Get()->flush();
        }


        // --- Public API ---
        bool SetupRegisterAllSWFFunctionsHook(uintptr_t moduleBase) {
            L.Get()->info("Setting up RegisterAllSWFFunctions hook...");
            uintptr_t targetAddress = moduleBase + REGISTER_ALL_SWF_FUNCTIONS_OFFSET;
            g_originalFunction = reinterpret_cast<OriginalRegisterAll_t>(targetAddress);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            LONG error = DetourAttach(&(PVOID&)g_originalFunction, DetouredRegisterAllSWFFunctions);
            if (error != NO_ERROR) {
                L.Get()->error("DetourAttach failed for RegisterAllSWFFunctions: {}", error);
                DetourTransactionAbort();
                g_originalFunction = nullptr;
                return false;
            }
            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("DetourTransactionCommit failed for RegisterAllSWFFunctions: {}", error);
                return false; // Indicate commit failure
            }

            L.Get()->info("RegisterAllSWFFunctions hook attached successfully at 0x{:X}", targetAddress);
            L.Get()->flush();
            return true;
        }
    }
}