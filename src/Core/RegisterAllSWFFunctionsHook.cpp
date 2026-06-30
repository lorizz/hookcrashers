#include "RegisterAllSWFFunctionsHook.h"
#include "../Util/Logger.h"
#include "../SWF/Custom/CustomFunctions.h"
#include "RegisterSWFFunctionHook.h"

namespace HookCrashers {
    namespace Core {
        static Util::Logger& L = Util::Logger::Instance();

        using OriginalRegisterAll_t = void(__fastcall*)(void* param_1);
        static OriginalRegisterAll_t g_originalFunction = nullptr;

        constexpr uintptr_t REGISTER_ALL_SWF_FUNCTIONS_OFFSET = 0x121500; // updated

        void __fastcall DetouredRegisterAllSWFFunctions(void* param_1) {
            if (g_originalFunction) {
                try {
                    g_originalFunction(param_1);
                }
                catch (...) {
                }
            }

            void* capturedThisPtr = Core::GetCorrectThisPtr();
            if (capturedThisPtr) {
                SWF::Custom::RegisterAllWithGame(capturedThisPtr, 0);
            }
        }

        bool SetupRegisterAllSWFFunctionsHook(uintptr_t moduleBase) {
            uintptr_t targetAddress = moduleBase + REGISTER_ALL_SWF_FUNCTIONS_OFFSET;
            g_originalFunction = reinterpret_cast<OriginalRegisterAll_t>(targetAddress);
            L.Get()->info("[Hook] Installing hook | name=RegisterAllSWFFunctions | RVA=0x{:X} | VA=0x{:X}.", REGISTER_ALL_SWF_FUNCTIONS_OFFSET, targetAddress);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            LONG error = DetourAttach(&(PVOID&)g_originalFunction, DetouredRegisterAllSWFFunctions);
            if (error != NO_ERROR) {
                L.Get()->error("[Hook] DetourAttach failed | name=RegisterAllSWFFunctions | RVA=0x{:X} | error={}", REGISTER_ALL_SWF_FUNCTIONS_OFFSET, error);
                DetourTransactionAbort();
                g_originalFunction = nullptr;
                return false;
            }
            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("[Hook] DetourTransactionCommit failed | name=RegisterAllSWFFunctions | RVA=0x{:X} | error={}", REGISTER_ALL_SWF_FUNCTIONS_OFFSET, error);
                return false;
            }

            L.Get()->info("[Hook] Hook installed | name=RegisterAllSWFFunctions | RVA=0x{:X} | VA=0x{:X}.", REGISTER_ALL_SWF_FUNCTIONS_OFFSET, targetAddress);
            return true;
        }
    }
}
