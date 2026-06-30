#include "RegisterSWFFunctionHook.h"
#include "../Util/Logger.h"

namespace HookCrashers {
    namespace Core {
        static Util::Logger& L = Util::Logger::Instance();

        using OriginalRegisterFunc_t = void(__thiscall*)(void* thisPtr, uint16_t functionId, const char* functionName);
        static OriginalRegisterFunc_t g_originalFunction = nullptr;

        static void* g_correctThisPtr = nullptr;

        constexpr uint16_t ID_FOR_THISPTR_CAPTURE = 0x14D;
        const char* NAME_FOR_THISPTR_CAPTURE = "u_temp";

        constexpr uintptr_t REGISTER_SWF_FUNCTION_OFFSET = 0xFD4B0; // updated

        void __fastcall DetouredRegisterSWFFunction(void* thisPtr, void* /* edx - unused dummy */, uint16_t functionId, const char* functionName) {
            if (thisPtr != nullptr && functionId == ID_FOR_THISPTR_CAPTURE && functionName != nullptr && strcmp(functionName, NAME_FOR_THISPTR_CAPTURE) == 0) {
                static void* lastLoggedPtr = nullptr;
                if (lastLoggedPtr != thisPtr) {
                    g_correctThisPtr = thisPtr;
                    lastLoggedPtr = g_correctThisPtr;
                }
                else {
                    g_correctThisPtr = thisPtr;
                }
            }
            if (g_originalFunction) {
                try {
                    g_originalFunction(thisPtr, functionId, functionName);
                }
                catch (const std::exception&) {
                }
                catch (...) {
                }
            }
        }

        bool SetupRegisterSWFFunctionHook(uintptr_t moduleBase) {
            uintptr_t targetAddress = moduleBase + REGISTER_SWF_FUNCTION_OFFSET;
            g_originalFunction = reinterpret_cast<OriginalRegisterFunc_t>(targetAddress);
            L.Get()->info("[Hook] Installing hook | name=RegisterSWFFunction | RVA=0x{:X} | VA=0x{:X}.", REGISTER_SWF_FUNCTION_OFFSET, targetAddress);

            if (!g_originalFunction) {
                L.Get()->error("[Hook] RegisterSWFFunction hook failed because the target address is invalid.");
                return false;
            }

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            LONG error = DetourAttach(&(PVOID&)g_originalFunction, DetouredRegisterSWFFunction);
            if (error != NO_ERROR) {
                L.Get()->error("[Hook] DetourAttach failed | name=RegisterSWFFunction | RVA=0x{:X} | error={}", REGISTER_SWF_FUNCTION_OFFSET, error);
                DetourTransactionAbort();
                g_originalFunction = nullptr;
                return false;
            }
            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("[Hook] DetourTransactionCommit failed | name=RegisterSWFFunction | RVA=0x{:X} | error={}", REGISTER_SWF_FUNCTION_OFFSET, error);
                g_originalFunction = nullptr;
                return false;
            }

            L.Get()->info("[Hook] Hook installed | name=RegisterSWFFunction | RVA=0x{:X} | VA=0x{:X}.", REGISTER_SWF_FUNCTION_OFFSET, targetAddress);
            return true;
        }

        void* GetCorrectThisPtr() {
            return g_correctThisPtr;
        }
    }
}
