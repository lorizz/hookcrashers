#include "RegisterSWFFunctionHook.h"
#include "../Util/Logger.h"

namespace HookCrashers {
    namespace Core {
        static Util::Logger& L = Util::Logger::Instance();

        using OriginalRegisterFunc_t = void(__thiscall*)(void* thisPtr, uint16_t functionId, const char* functionName);
        static OriginalRegisterFunc_t g_originalFunction = nullptr;

        static void* g_correctThisPtr = nullptr;

        constexpr uint16_t ID_FOR_THISPTR_CAPTURE = 0x13A;
        const char* NAME_FOR_THISPTR_CAPTURE = "u_temp";

        constexpr uintptr_t REGISTER_SWF_FUNCTION_OFFSET = 0x5D370;

        void __fastcall DetouredRegisterSWFFunction(void* thisPtr, void* /* edx - unused dummy */, uint16_t functionId, const char* functionName) {
            if (thisPtr != nullptr && functionId == ID_FOR_THISPTR_CAPTURE && functionName != nullptr && strcmp(functionName, NAME_FOR_THISPTR_CAPTURE) == 0) {
                static void* lastLoggedPtr = nullptr;
                if (lastLoggedPtr != thisPtr) {
                    g_correctThisPtr = thisPtr; 
                    L.Get()->info("Updated/Captured correct 'this' pointer: 0x{:X} (from '{}' ID:{:#x})",
                        reinterpret_cast<uintptr_t>(g_correctThisPtr), NAME_FOR_THISPTR_CAPTURE, ID_FOR_THISPTR_CAPTURE);
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
                catch (const std::exception& e) {
                    L.Get()->error("!!! std::exception in original RegisterSWFFunction (ID={:#x}, Name={}): {} !!!",
                        functionId, functionName ? functionName : "NULL", e.what());
                }
                catch (...) {
                    L.Get()->critical("!!! Unknown exception in original RegisterSWFFunction (ID={:#x}, Name={}) !!!",
                        functionId, functionName ? functionName : "NULL");
                }
            }
            else {
                L.Get()->error("Original RegisterSWFFunction pointer is null! Cannot call original for ID={:#x}, Name={}.",
                    functionId, functionName ? functionName : "NULL");
            }
        }

        bool SetupRegisterSWFFunctionHook(uintptr_t moduleBase) {
            L.Get()->info("Setting up RegisterSWFFunction hook...");
            uintptr_t targetAddress = moduleBase + REGISTER_SWF_FUNCTION_OFFSET;
            g_originalFunction = reinterpret_cast<OriginalRegisterFunc_t>(targetAddress);

            if (!g_originalFunction) {
                L.Get()->error("Target address for RegisterSWFFunction is invalid (0x{:X})", targetAddress);
                return false;
            }

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            LONG error = DetourAttach(&(PVOID&)g_originalFunction, DetouredRegisterSWFFunction);
            if (error != NO_ERROR) {
                L.Get()->error("DetourAttach failed for RegisterSWFFunction: {}", error);
                DetourTransactionAbort();
                g_originalFunction = nullptr;
                return false;
            }
            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("DetourTransactionCommit failed for RegisterSWFFunction: {}", error);
                g_originalFunction = nullptr;
                return false;
            }

            L.Get()->info("RegisterSWFFunction hook attached successfully at 0x{:X}", targetAddress);
            L.Get()->flush();
            return true;
        }

        void* GetCorrectThisPtr() {
            return g_correctThisPtr;
        }
    }
}