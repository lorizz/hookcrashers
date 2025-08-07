#include "UIInputHandlerHook.h"
#include "../Util/Logger.h"
#include <detours.h>
#include <windows.h>
#include <cstdint>
#include <unordered_set>

namespace HookCrashers {
    namespace Core {
        static Util::Logger& L = Util::Logger::Instance();
        
        static bool g_rightArrowWasPressed = false;
        static bool g_leftArrowWasPressed = false;

        // FIXED: Use __fastcall which is binary-compatible with __thiscall for this case
        // __fastcall: ECX = this_ptr, EDX = buttonId (or stack if doesn't fit)
        using OriginalUIInputHandler_t = uint32_t(__thiscall*)(void* this_ptr, uint32_t buttonId);
        static OriginalUIInputHandler_t g_originalUIInputHandler = nullptr;

        constexpr uintptr_t UI_INPUT_HANDLER_OFFSET = 0x119EA0;

        static std::unordered_set<uint32_t> g_discoveredFrameIds;

        // Use __fastcall instead of __thiscall for the detour function
        uint32_t __fastcall DetouredUIInputHandler(void* this_ptr, void* /* edx */, uint32_t buttonId) {

            // 1. Chiama la funzione originale per non rompere il gioco.
            // Salviamo il suo valore di ritorno per restituirlo alla fine.
            uint32_t returnValue = 0;
            if (g_originalUIInputHandler) {
                returnValue = g_originalUIInputHandler(this_ptr, buttonId);
            }

            // 2. Leggi il frame che il gioco HA APPENA selezionato.
            uint32_t selectedFrame = *(uint32_t*)((uintptr_t)this_ptr + 0xE0);

            // 3. Controlla se abbiamo già visto questo frame ID.
            //    g_discoveredFrameIds.find(...) == g_discoveredFrameIds.end() significa "non trovato".
            if (g_discoveredFrameIds.find(selectedFrame) == g_discoveredFrameIds.end()) {

                // È un nuovo frame!
                // a) Aggiungilo al nostro set per non loggarlo di nuovo.
                g_discoveredFrameIds.insert(selectedFrame);

                // b) Loggalo, così possiamo vederlo.
                //L.Get()->info("[Frame Scanner] Discovered new selectable frame ID: {}", selectedFrame);
                //L.Get()->flush();
            }

            // 4. Restituisci il valore originale per mantenere il gioco stabile.
            return returnValue;
        }

        bool SetupUIInputHandlerHook(uintptr_t moduleBase) {
            uintptr_t targetAddress = moduleBase + UI_INPUT_HANDLER_OFFSET;
            g_originalUIInputHandler = reinterpret_cast<OriginalUIInputHandler_t>(targetAddress);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            LONG error = DetourAttach(&(PVOID&)g_originalUIInputHandler, DetouredUIInputHandler);
            if (error != NO_ERROR) {
                L.Get()->error("DetourAttach failed for UIInputHandlerHook: {}", error);
                DetourTransactionAbort();
                return false;
            }

            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("DetourTransactionCommit failed for UIInputHandlerHook: {}", error);
                return false;
            }

            L.Get()->info("UI Input Handler hook attached successfully!");
            L.Get()->flush();
            return true;
        }
    } // namespace Core
} // namespace HookCrashers