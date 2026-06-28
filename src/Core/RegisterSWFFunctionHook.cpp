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
            static int s_callCount = 0;
            ++s_callCount;
            const bool logThisCall = s_callCount <= 40 || (s_callCount % 100) == 0;
            if (logThisCall) {
                L.Get()->info(
                    "[HookHit] RegisterSWFFunction ENTER call={} this=0x{:X} function_id={} name='{}'.",
                    s_callCount,
                    reinterpret_cast<uintptr_t>(thisPtr),
                    functionId,
                    functionName ? functionName : "<null>");
                L.Get()->flush();
            }

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
                    if (logThisCall) {
                        L.Get()->info("[HookHit] RegisterSWFFunction LEAVE original call={}", s_callCount);
                        L.Get()->flush();
                    }
                }
                catch (const std::exception& e) {
                    L.Get()->critical("[HookHit] RegisterSWFFunction original threw std::exception: {}", e.what());
                    L.Get()->flush();
                }
                catch (...) {
                    L.Get()->critical("[HookHit] RegisterSWFFunction original threw unknown exception.");
                    L.Get()->flush();
                }
            }
            else {
                L.Get()->error("[HookHit] RegisterSWFFunction original pointer is null.");
                L.Get()->flush();
            }
        }

        bool SetupRegisterSWFFunctionHook(uintptr_t moduleBase) {
            uintptr_t targetAddress = moduleBase + REGISTER_SWF_FUNCTION_OFFSET;
            g_originalFunction = reinterpret_cast<OriginalRegisterFunc_t>(targetAddress);
            L.Get()->info("[Hook] Attaching RegisterSWFFunction hook at offset 0x{:X} (address=0x{:X}).", REGISTER_SWF_FUNCTION_OFFSET, targetAddress);

            if (!g_originalFunction) {
                L.Get()->error("[Hook] RegisterSWFFunction hook failed because the target address is invalid.");
                return false;
            }

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            LONG error = DetourAttach(&(PVOID&)g_originalFunction, DetouredRegisterSWFFunction);
            if (error != NO_ERROR) {
                L.Get()->error("[Hook] RegisterSWFFunction DetourAttach failed at offset 0x{:X}: {}", REGISTER_SWF_FUNCTION_OFFSET, error);
                DetourTransactionAbort();
                g_originalFunction = nullptr;
                return false;
            }
            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("[Hook] RegisterSWFFunction DetourTransactionCommit failed at offset 0x{:X}: {}", REGISTER_SWF_FUNCTION_OFFSET, error);
                g_originalFunction = nullptr;
                return false;
            }

            L.Get()->info("[Hook] RegisterSWFFunction hook attached successfully at offset 0x{:X} (address=0x{:X}).", REGISTER_SWF_FUNCTION_OFFSET, targetAddress);
            return true;
        }

        void* GetCorrectThisPtr() {
            return g_correctThisPtr;
        }
    }
}
