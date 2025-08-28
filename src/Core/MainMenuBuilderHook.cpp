#include <windows.h>
#include <detours.h>
#include <cstdint>
#include <vector>
#include <new>

// Assicurati che questi percorsi siano corretti per il tuo progetto
#include "../Util/Logger.h"
#include "../Util/MemoryPatcher.h"

namespace HookCrashers {
    namespace Core {
        // Istanza del logger per la diagnostica
        static Util::Logger& L = Util::Logger::Instance();

        // === Typedef e Puntatori a Funzione ===
        using MainMenuBuilder_t = void* (__thiscall*)(void* this_ptr, void* param_2, void* param_3);
        using SetMenuState_t = void(__thiscall*)(void* this_ptr, uint16_t menu_id, void* pMenuObject);
        using LoadSymbol_t = void* (__thiscall*)(void* this_ptr_handler, void* pSceneObject, void* pStringObject, uint32_t someID, float x, float y);
        using PrepareStringObject_t = char* (__thiscall*)(void* pStringObjectBuffer, const char* c_string);
        using FinalizeAndAddElement_t = void(__thiscall*)(void* pHandlerData, void* pElement);

        // Puntatori alle funzioni originali e del gioco
        static MainMenuBuilder_t g_originalMainMenuBuilder = nullptr;
        static SetMenuState_t SetMenuState = nullptr;
        static LoadSymbol_t LoadSymbol = nullptr;
        static PrepareStringObject_t PrepareStringObject = nullptr;
        static FinalizeAndAddElement_t FinalizeAndAddElement = nullptr;

        // Puntatore globale al nostro gestore di menu personalizzato (utile per il debug)
        static void* g_pFirstTimeMenuHandler = nullptr;

        // === Costanti e Offset ===
        constexpr uintptr_t MAIN_MENU_BUILDER_OFFSET = 0x1417C0; // 0xaf17c0 - 0x9b0000
        constexpr uintptr_t SET_MENU_STATE_OFFSET = 0xAD99A0 - 0x9B0000; // 0x1299A0 (Verifica questo offset)
        constexpr uintptr_t LOAD_SYMBOL_OFFSET = 0x152B20;
        constexpr uintptr_t PREPARE_STRING_OBJECT_OFFSET = 0x2EC30;
        constexpr uintptr_t FINALIZE_AND_ADD_ELEMENT_OFFSET = 0x14F860;

        constexpr size_t HANDLER_SIZE = 0x20C;
        constexpr size_t ORIGINAL_HANDLER_COUNT = 46; // 0x2E
        constexpr size_t NEW_HANDLER_COUNT = 47;     // 0x2F
        constexpr size_t NEW_HANDLER_SIZE_BYTES = NEW_HANDLER_COUNT * HANDLER_SIZE; // 0x6038

        constexpr uint16_t BUTTON_CHOICES_ID = 0x17; // ID del menu da usare come template
        constexpr uint16_t FIRST_TIME_MENU_ID = ORIGINAL_HANDLER_COUNT; // Il nostro nuovo ID (46)

        // Funzione di utilità per determinare se mostrare il menu speciale
        bool IsFirstLaunch() {
            // TODO: Implementa una logica reale qui (es. controllo file di configurazione)
            return true;
        }

        // Funzione per applicare le patch in memoria alla funzione MainMenuBuilder
        bool PatchGameForNewMenu() {
            Util::MemoryPatcher patcher;
            L.Get()->info("[Patcher] Applying patches to MainMenuBuilder...");

            // --- Patch 1: Dimensione Totale della Memoria ---
            // Indirizzo: 00af18b0 | PUSH 0x5e2c
            // Dobbiamo cambiare il valore 0x5e2c (46*0x20c) in 0x6038 (47*0x20c)
            const uintptr_t sizePatchOffset = 0x1418B0;
            const uint32_t newSize = NEW_HANDLER_SIZE_BYTES;

            // L'istruzione PUSH imm32 ha l'opcode 0x68. Il valore da patchare inizia all'offset + 1
            if (!patcher.PatchBytes(sizePatchOffset + 1, {
                (uint8_t)(newSize & 0xFF),
                (uint8_t)((newSize >> 8) & 0xFF),
                (uint8_t)((newSize >> 16) & 0xFF),
                (uint8_t)((newSize >> 24) & 0xFF)
                })) {
                L.Get()->critical("[Patcher] FAILED to patch menu handler allocation size!");
                return false;
            }
            L.Get()->info("[Patcher] Menu handler allocation size patched to 0x{:X}", newSize);

            // --- Patch 2: Numero di Elementi ---
            // Indirizzo: 00af18e3 | PUSH 0x2e
            // Dobbiamo cambiare il valore 0x2e (46) in 0x2f (47)
            const uintptr_t countPatchOffset = 0x1418E3;
            const uint8_t newCount = NEW_HANDLER_COUNT;

            // L'istruzione PUSH imm8 ha l'opcode 0x6A. Il valore da patchare è all'offset + 1
            if (!patcher.PatchBytes(countPatchOffset + 1, { newCount })) {
                L.Get()->critical("[Patcher] FAILED to patch menu handler count!");
                return false; // In un'implementazione reale, si potrebbe voler ripristinare la patch precedente
            }
            L.Get()->info("[Patcher] Menu handler count patched to {}", newCount);

            return true;
        }

        // La nostra funzione hook per MainMenuBuilder - ora molto più sicura
        void* __fastcall HookedMainMenuBuilder(void* this_ptr, void* edx_dummy, void* param_2, void* param_3)
        {
            static bool hasRunOnce = false;

            // Esegui la funzione originale, che (grazie alle nostre patch) allocherà spazio per 47 elementi.
            void* result = g_originalMainMenuBuilder(this_ptr, param_2, param_3);

            if (IsFirstLaunch() && !hasRunOnce)
            {
                hasRunOnce = true;
                L.Get()->info("[MainMenuBuilder] Hook triggered for first launch.");

                // Ottieni il puntatore all'array di gestori
                uintptr_t* pHandlerArrayPtr = (uintptr_t*)((uintptr_t)this_ptr + 0x40);
                uintptr_t pHandlerArray = *pHandlerArrayPtr;
                if (!pHandlerArray) {
                    L.Get()->error("[MainMenuBuilder] Handler array is null after creation!");
                    return result;
                }

                // Calcola l'indirizzo del nostro nuovo slot (il 47°, indice 46)
                void* pOurHandlerSlot = (void*)(pHandlerArray + (ORIGINAL_HANDLER_COUNT * HANDLER_SIZE));
                g_pFirstTimeMenuHandler = pOurHandlerSlot;

                // Inizializza il nostro slot copiando da un gestore esistente e valido (0x17).
                // Questo assicura che tutti i puntatori a funzione e i membri di base siano validi.
                void* pTemplateHandler = (void*)(pHandlerArray + (BUTTON_CHOICES_ID * HANDLER_SIZE));
                memcpy(pOurHandlerSlot, pTemplateHandler, HANDLER_SIZE);

                // Ora personalizziamo il nostro gestore caricando il simbolo "firsttime"
                void* pSceneObject = *(void**)((uintptr_t)this_ptr + 4);
                if (pSceneObject) {
                    uint8_t tempStringObj[0x40]; // Buffer temporaneo per l'oggetto stringa del gioco
                    PrepareStringObject(tempStringObj, "firsttime");

                    void* pFirstTimeMenu = LoadSymbol(pOurHandlerSlot, pSceneObject, tempStringObj, 0x7E3, 0.0f, 0.0f);
                    if (pFirstTimeMenu) {
                        FinalizeAndAddElement(pOurHandlerSlot, pFirstTimeMenu);
                        L.Get()->debug("[MainMenuBuilder] Custom 'firsttime' symbol loaded and added successfully.");
                    }
                    else {
                        L.Get()->warn("[MainMenuBuilder] LoadSymbol for 'firsttime' returned null.");
                    }
                }
                else {
                    L.Get()->warn("[MainMenuBuilder] pSceneObject is null, cannot load custom symbol.");
                }

                // Ora che il nostro gestore è pronto, impostiamolo come stato attivo del menu.
                // Questa chiamata è sicura perché pOurHandlerSlot punta a memoria valida gestita dal gioco.
                L.Get()->info("[MainMenuBuilder] Calling SetMenuState for 'firsttime' menu (ID: {}).", FIRST_TIME_MENU_ID);
                SetMenuState(this_ptr, FIRST_TIME_MENU_ID, g_pFirstTimeMenuHandler);
            }

            return result;
        }

        // Funzione principale per installare l'hook
        bool SetupMainMenuBuilderHook(uintptr_t moduleBase)
        {
            // PASSO 1: Patcha la funzione originale PRIMA di installare l'hook.
            // Questo è il passaggio più importante per la stabilità.
            if (!PatchGameForNewMenu()) {
                L.Get()->critical("Failed to apply memory patches. Aborting hook setup.");
                return false;
            }

            // PASSO 2: Inizializza i puntatori alle funzioni del gioco.
            LoadSymbol = reinterpret_cast<LoadSymbol_t>(moduleBase + LOAD_SYMBOL_OFFSET);
            PrepareStringObject = reinterpret_cast<PrepareStringObject_t>(moduleBase + PREPARE_STRING_OBJECT_OFFSET);
            FinalizeAndAddElement = reinterpret_cast<FinalizeAndAddElement_t>(moduleBase + FINALIZE_AND_ADD_ELEMENT_OFFSET);
            g_originalMainMenuBuilder = reinterpret_cast<MainMenuBuilder_t>(moduleBase + MAIN_MENU_BUILDER_OFFSET);
            SetMenuState = reinterpret_cast<SetMenuState_t>(moduleBase + SET_MENU_STATE_OFFSET);

            // PASSO 3: Installa l'hook usando Detours.
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            LONG error = DetourAttach(&(PVOID&)g_originalMainMenuBuilder, HookedMainMenuBuilder);
            if (error != NO_ERROR) {
                L.Get()->error("DetourAttach failed for MainMenuBuilder with error code: {}", error);
                DetourTransactionAbort();
                return false;
            }

            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("DetourTransactionCommit failed with error code: {}", error);
                return false;
            }

            L.Get()->info("MainMenuBuilder hook installed successfully.");
            return true;
        }
    } // namespace Core
} // namespace HookCrashers