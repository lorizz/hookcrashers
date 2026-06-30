#include "DecryptSaveFileHook.h"
#include "../Util/Logger.h"
#include <detours.h>
#include "../../include/HookCrashers/Public/Globals.h"

namespace HookCrashers {
    namespace Core {

        static Util::Logger& L = Util::Logger::Instance();

        using DecryptFunc_t = void(__thiscall*)(void* thisPtr, void* buffer_in_out, void* buffer_out_copy, int size);
        static DecryptFunc_t g_originalDecryptFunction = nullptr;
        constexpr uintptr_t DECRYPT_FUNCTION_OFFSET = 0xDDD50; // updated

        void __fastcall DetouredDecryptFunction(void* thisPtr, void* edx_dummy, void* buffer_in_out, void* buffer_out_copy, int size) {
            if (g_decryptThisPtr == nullptr || g_decryptThisPtr != thisPtr) {
                g_decryptThisPtr = thisPtr;
            }
            g_originalDecryptFunction(thisPtr, buffer_in_out, buffer_out_copy, size);
        }

        bool SetupDecryptSaveFileHook(uintptr_t moduleBase) {
            if (g_originalDecryptFunction) {
                L.Get()->warn("[Hook] Hook already installed | name=DecryptSaveFile.");
                return true;
            }
            uintptr_t targetAddress = moduleBase + DECRYPT_FUNCTION_OFFSET;
            L.Get()->info("[Hook] Installing hook | name=DecryptSaveFile | RVA=0x{:X} | VA=0x{:X}.", DECRYPT_FUNCTION_OFFSET, targetAddress);
            g_originalDecryptFunction = reinterpret_cast<DecryptFunc_t>(targetAddress);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            LONG error = DetourAttach(&(PVOID&)g_originalDecryptFunction, (void*)DetouredDecryptFunction);
            if (error != NO_ERROR) {
                L.Get()->error("[Hook] DetourAttach failed | name=DecryptSaveFile | error={}", error);
                DetourTransactionAbort();
                g_originalDecryptFunction = nullptr;
                return false;
            }

            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("[Hook] DetourTransactionCommit failed | name=DecryptSaveFile | error={}", error);
                g_originalDecryptFunction = nullptr;
                return false;
            }

            L.Get()->info("[Hook] Hook installed | name=DecryptSaveFile | RVA=0x{:X} | VA=0x{:X}.", DECRYPT_FUNCTION_OFFSET, targetAddress);
            return true;
        }
    }
}