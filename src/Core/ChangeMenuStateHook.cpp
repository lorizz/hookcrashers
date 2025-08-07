#include "ChangeMenuStateHook.h"
#include "SceneConstructorHook.h" // Per accedere a g_pMyBigDataBlock
#include "../Util/Logger.h"      // Il tuo sistema di logging
#include <detours.h>
#include <windows.h>
#include <cstdint>

namespace HookCrashers {
    namespace Core {
        static Util::Logger& L = Util::Logger::Instance();

        // Variabile globale che punta al nostro blocco dati handler, definita in SceneConstructorHook.cpp
        extern void* g_pMyBigDataBlock;

        // ID che abbiamo scelto per il nostro menu. Deve corrispondere a quello in MainMenuSystemHook.cpp
        constexpr uint16_t MODLOADER_MENU_ID = 0x100;

        // Tipi di funzione per le funzioni del gioco che dobbiamo chiamare o deviare
        using ChangeMenuState_t = void(__thiscall*)(void* this_ptr, uint16_t menu_id);
        using ActivateDeactivateHandler_t = void(__thiscall*)(void* handler_ptr, int activate_flag); // Questa è FUN_00d49130
        using NotifyStateChange_t = void* (__thiscall*)(void* this_ptr, uint16_t prev_menu_id, uint16_t prev_state_id, uint8_t flag1, uint8_t flag2, uint8_t flag3); // Questa è FUN_00d2ac70

        // Puntatori alle funzioni
        static ChangeMenuState_t g_originalChangeMenuState = nullptr;
        static ActivateDeactivateHandler_t ActivateDeactivateHandler = nullptr;
        static NotifyStateChange_t NotifyStateChange = nullptr;

        // Offset delle funzioni nel file eseguibile del gioco
        constexpr uintptr_t CHANGE_MENU_STATE_OFFSET = 0x8AB40;
        constexpr uintptr_t ACTIVATE_DEACTIVATE_HANDLER_OFFSET = 0xA9130; // Offset di FUN_00d49130
        constexpr uintptr_t NOTIFY_STATE_CHANGE_OFFSET = 0x8AC70; // Offset di FUN_00d2ac70

        /// <summary>
        /// La nostra versione hookata di ChangeMenuState.
        /// Funge da centralino: se l'ID è il nostro, gestiamo la logica noi, altrimenti
        /// passiamo la chiamata alla funzione originale del gioco.
        /// </summary>
        void __fastcall HookedChangeMenuState(void* this_ptr, void* edx_dummy, uint16_t menu_id)
        {
            // --- Centralino ---
            if (menu_id != MODLOADER_MENU_ID)
            {
                // Non è il nostro menu, lascia che il gioco faccia il suo lavoro.
                g_originalChangeMenuState(this_ptr, menu_id);
                return;
            }

            // --- È IL NOSTRO MENU! ESEGUIAMO LA NOSTRA LOGICA, REPLICANDO L'ORIGINALE ---
            L.Get()->info("HookedChangeMenuState: Switching to custom ModLoader Menu (ID: 0x{:X})", menu_id);

            // Puntatori a membri importanti dell'oggetto 'this_ptr' (che è il gestore del menu)
            void** pCurrentHandlerPtr = (void**)((uintptr_t)this_ptr + 0x38);
            uint16_t* pCurrentStateIdPtr = (uint16_t*)((uintptr_t)this_ptr + 0x3C);

            // 1. Salva lo stato precedente e disattiva il vecchio handler
            uint16_t previous_menu_id_for_transition = 0xFFFE;
            if (*pCurrentHandlerPtr != nullptr) {
                ActivateDeactivateHandler(*pCurrentHandlerPtr, 0); // Disattiva (flag 0)
                previous_menu_id_for_transition = *(uint16_t*)((uintptr_t)(*pCurrentHandlerPtr) + 0xE);
            }
            uint16_t previous_state_id = *pCurrentStateIdPtr;

            // 2. Imposta il nuovo ID di stato
            *pCurrentStateIdPtr = menu_id;

            // 3. Imposta il PUNTATORE AL NOSTRO handler come attivo
            void* new_handler = g_pMyBigDataBlock;
            *pCurrentHandlerPtr = new_handler;

            // 4. Passa l'ID del menu precedente al nuovo handler (per le transizioni)
            if (*(char*)((uintptr_t)new_handler + 0x14) != '\0') {
                *(uint16_t*)((uintptr_t)new_handler + 0x10) = previous_menu_id_for_transition;
                *(uint16_t*)((uintptr_t)new_handler + 0xE) = previous_menu_id_for_transition;
            }

            // 5. Attiva il nostro nuovo handler
            ActivateDeactivateHandler(new_handler, 1); // Attiva (flag 1)

            // 6. Esegui la callback "OnEnter" del nostro handler, se esiste
            *(uint16_t*)((uintptr_t)new_handler + 0x1F1) = 0; // Azzera un flag
            void* pOnEnterFunc = *(void**)((uintptr_t)new_handler + 0x1D0);
            if (pOnEnterFunc) {
                void* pContext = *(void**)((uintptr_t)new_handler + 0x1AC);
                ((void(__thiscall*)(void*))pOnEnterFunc)(pContext);
            }

            // 7. Chiama la funzione di notifica finale, per mantenere lo stato del gioco coerente
            uint16_t handler_flags = *(uint16_t*)((uintptr_t)new_handler + 0x12);
            NotifyStateChange(this_ptr, previous_menu_id_for_transition, previous_state_id,
                (uint8_t)(handler_flags >> 1) & 1,
                (uint8_t)handler_flags & 1,
                (uint8_t)(handler_flags >> 3) & 1);

            L.Get()->info("Custom menu state switch complete.");
            L.Get()->flush();
        }

        /// <summary>
        /// Inizializza i puntatori alle funzioni e installa il detour per ChangeMenuState.
        /// </summary>
        bool SetupChangeMenuStateHook(uintptr_t moduleBase)
        {
            // Risolvi gli indirizzi delle funzioni del gioco che ci servono
            ActivateDeactivateHandler = (ActivateDeactivateHandler_t)(moduleBase + ACTIVATE_DEACTIVATE_HANDLER_OFFSET);
            NotifyStateChange = (NotifyStateChange_t)(moduleBase + NOTIFY_STATE_CHANGE_OFFSET);
            g_originalChangeMenuState = (ChangeMenuState_t)(moduleBase + CHANGE_MENU_STATE_OFFSET);

            // Installa il detour
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            LONG error = DetourAttach(&(PVOID&)g_originalChangeMenuState, HookedChangeMenuState);

            if (error != NO_ERROR) {
                L.Get()->error("DetourAttach for ChangeMenuState failed: {}", error);
                DetourTransactionAbort();
                return false;
            }

            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("DetourTransactionCommit for ChangeMenuStateHook failed! Error: {}", error);
                return false;
            }

            L.Get()->info("--- ChangeMenuStateHook attached successfully. ---");
            L.Get()->flush();
            return true;
        }
    }
}