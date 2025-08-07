#include "RegisterAllSWFFunctionsHook.h"
#include "../Util/Logger.h"
#include "../SWF/Custom/CustomFunctions.h"
#include "RegisterSWFFunctionHook.h"

namespace HookCrashers {
    namespace Core {
        static Util::Logger& L = Util::Logger::Instance();

        using OriginalRegisterAll_t = void(__fastcall*)(void* param_1);
        static OriginalRegisterAll_t g_originalFunction = nullptr;

        constexpr uintptr_t REGISTER_ALL_SWF_FUNCTIONS_OFFSET = 0x11E9E0;

        void __fastcall DetouredRegisterAllSWFFunctions(void* param_1) {
            //L.Get()->info("RegisterAllSWFFunctions hook executing...");

            if (g_originalFunction) {
                //L.Get()->debug("Calling original RegisterAllSWFFunctions...");
                try {
                    g_originalFunction(param_1);
                    //L.Get()->debug("Original RegisterAllSWFFunctions finished.");
                }
                catch (...) {
                    //L.Get()->critical("!!! Exception in original RegisterAllSWFFunctions !!!");
                }
            }
            else {
                //L.Get()->error("Original RegisterAllSWFFunctions pointer is null! Cannot call original.");
            }

            //L.Get()->debug("Attempting to register custom SWF functions with the game...");
            void* capturedThisPtr = Core::GetCorrectThisPtr();

            if (capturedThisPtr) {
                SWF::Custom::RegisterAllWithGame(capturedThisPtr, 0);
            }
            else {
                //L.Get()->error("Could not register custom functions: Captured 'this' pointer is NULL.");
            }

            //L.Get()->info("RegisterAllSWFFunctions hook finished.");
            //L.Get()->flush();
        }


        bool SetupRegisterAllSWFFunctionsHook(uintptr_t moduleBase) {
            //L.Get()->info("Setting up RegisterAllSWFFunctions hook...");
            uintptr_t targetAddress = moduleBase + REGISTER_ALL_SWF_FUNCTIONS_OFFSET;
            g_originalFunction = reinterpret_cast<OriginalRegisterAll_t>(targetAddress);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            LONG error = DetourAttach(&(PVOID&)g_originalFunction, DetouredRegisterAllSWFFunctions);
            if (error != NO_ERROR) {
                //L.Get()->error("DetourAttach failed for RegisterAllSWFFunctions: {}", error);
                DetourTransactionAbort();
                g_originalFunction = nullptr;
                return false;
            }
            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                //L.Get()->error("DetourTransactionCommit failed for RegisterAllSWFFunctions: {}", error);
                return false;
            }

            //L.Get()->info("RegisterAllSWFFunctions hook attached successfully at 0x{:X}", targetAddress);
            //L.Get()->flush();
            return true;
        }
    }
}