#include "DecryptSaveFileHook.h"
#include "../Util/Logger.h"
#include <detours.h>
#include "../../include/HookCrashers/Public/Globals.h"

namespace HookCrashers {
    namespace Core {

        static Util::Logger& L = Util::Logger::Instance();

        using DecryptFunc_t = void(__thiscall*)(void* thisPtr, void* buffer_in_out, void* buffer_out_copy, int size);
        static DecryptFunc_t g_originalDecryptFunction = nullptr;
        constexpr uintptr_t DECRYPT_FUNCTION_OFFSET = 0xDB470;

        void __fastcall DetouredDecryptFunction(void* thisPtr, void* edx_dummy, void* buffer_in_out, void* buffer_out_copy, int size) {
            if (g_decryptThisPtr == nullptr || g_decryptThisPtr != thisPtr) {
                g_decryptThisPtr = thisPtr;
			}
            g_originalDecryptFunction(thisPtr, buffer_in_out, buffer_out_copy, size);
        }

        bool SetupDecryptSaveFileHook(uintptr_t moduleBase) {
            if (g_originalDecryptFunction) {
                L.Get()->warn("Hook per Decrypt function gia' installato.");
                return true;
            }
            L.Get()->info("Setup del hook per Decrypt function...");
            uintptr_t targetAddress = moduleBase + DECRYPT_FUNCTION_OFFSET;
            g_originalDecryptFunction = reinterpret_cast<DecryptFunc_t>(targetAddress);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            LONG error = DetourAttach(&(PVOID&)g_originalDecryptFunction, (void*)DetouredDecryptFunction);
            if (error != NO_ERROR) {
                L.Get()->error("DetourAttach fallito per Decrypt function: {}", error);
                DetourTransactionAbort();
                g_originalDecryptFunction = nullptr;
                return false;
            }

            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("DetourTransactionCommit fallito per Decrypt function: {}", error);
                g_originalDecryptFunction = nullptr;
                return false;
            }

            L.Get()->info("Hook per Decrypt function agganciato con successo a 0x{:X}", targetAddress);
            return true;
        }
    }
}