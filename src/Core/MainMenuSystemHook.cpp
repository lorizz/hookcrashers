#include "MainMenuSystemHook.h"
#include "SceneConstructorHook.h"
#include "../Util/Logger.h"
#include <detours.h>
#include <windows.h>
#include <cstdint>

namespace HookCrashers {
    namespace Core {
        static Util::Logger& L = Util::Logger::Instance();

        using CreateButtonObject_t = void* (__thiscall*)(void*, void*, void*, uint32_t, uint32_t, uint8_t);
        using FinalizeAndAddElement_t = void(__thiscall*)(void* this_ptr, void* pElement);
        using MainMenuSceneConstructor_t = void(__thiscall*)(void* this_ptr, void* pSceneObject);
        using PrepareStringObject_t = char* (__thiscall*)(void* pStringObjectBuffer, const char* c_string);
        using LoadSymbol_t = void* (__thiscall*)(void* pMemoryManager, void* pSceneObject, void* pStringObject, uint32_t someID, float x, float y);
        using ChangeMenuState_t = void(__thiscall*)(void*, int);
        using TransitionControl_t = char(__thiscall*)(void*, uint16_t, uint16_t, uint8_t, uint8_t, uint8_t, void*, void*);

        static MainMenuSceneConstructor_t g_originalMainMenuSceneConstructor = nullptr;
        static AllocateData_t g_originalAllocateData = nullptr;

        static CreateButtonObject_t CreateButtonObject = nullptr;
        AllocateData_t AllocateData = nullptr;
        static FinalizeAndAddElement_t FinalizeAndAddElement = nullptr;
        static PrepareStringObject_t PrepareStringObject = nullptr;
        static LoadSymbol_t LoadSymbol = nullptr;
        static ChangeMenuState_t ChangeMenuState = nullptr;
        static TransitionControl_t TransitionControl = nullptr;

        void* g_pMemoryManager = nullptr;
        static void* g_buttonDispatcherFunc = nullptr;
        static void* g_myCustomSceneObject = nullptr;
        static bool g_modloaderMenuLoaded = false;

        constexpr uintptr_t CREATE_BUTTON_OFFSET = 0xA9DA0;
        constexpr uintptr_t ALLOCATE_DATA_OFFSET = 0x40CA0;
        constexpr uintptr_t MAIN_MENU_SCENE_CONSTRUCTOR_OFFSET = 0xA0220;
        constexpr uintptr_t FINALIZE_AND_ADD_ELEMENT_OFFSET = 0xA8610;
        constexpr uintptr_t BUTTON_DISPATCHER_OFFSET = 0xA15C0;
        constexpr uintptr_t PREPARE_STRING_OBJECT_OFFSET = 0x221B0;
        constexpr uintptr_t LOAD_SYMBOL_OFFSET = 0xAB5C0;
        constexpr uintptr_t CHANGE_MENU_STATE_OFFSET = 0x8AB40;
        constexpr uintptr_t TRANSITION_CONTROL_OFFSET = 0x8AC70;

        constexpr uintptr_t MODLOADER_MENU_PTR_OFFSET_IN_SCENE = 0x184;

        constexpr uint16_t MY_CUSTOM_BUTTON_INDEX = 9;

        constexpr uint16_t MAIN_MENU_ID = 0x15;
        constexpr uint16_t MODLOADER_MENU_ID = 0x28;

        void __cdecl MyCustomButton_Select(void* pButtonContainer) {
            if (!pButtonContainer) return;
            void* pSceneObject = *(void**)((uintptr_t)pButtonContainer + 0x38);
            if (!pSceneObject) return;

            uint16_t* pCurrentSelectedIndex = (uint16_t*)((uintptr_t)pSceneObject + 0xC);

            if (*pCurrentSelectedIndex != MY_CUSTOM_BUTTON_INDEX) {
                *pCurrentSelectedIndex = MY_CUSTOM_BUTTON_INDEX;
            }
        }
        extern void* g_pModLoaderMenuObject;

        void __cdecl MyCustomButton_ActionCallback(void* pButtonContainer) {
            L.Get()->info("Custom Button Action Executed.");

            if (!pButtonContainer) return;

            // 1. Replicare la chiamata a TransitionControl ESATTAMENTE come fa ButtonSixAction.

            // Ottieni il puntatore all'handler del menu attivo (MainMenuScene)
            void* pMainMenuScene = *(void**)((uintptr_t)pButtonContainer + 0x38);
            if (!pMainMenuScene) {
                L.Get()->error("Could not get MainMenuScene handler from pButtonContainer.");
                return;
            }

            // Ottieni l'ID del bottone attualmente selezionato (su cui è il cursore)
            // Questo è il valore che il gioco passa come primo parametro.
            uint16_t selected_button_id = *(uint16_t*)((uintptr_t)pMainMenuScene + 0xE);
            L.Get()->debug("Currently selected button ID is: {}", selected_button_id);

            // Chiama la funzione di transizione con i parametri corretti.
            // L'ID del bottone è dinamico, non fisso.
            char transitionResult = TransitionControl(
                pButtonContainer,       // this pointer (come fa l'originale)
                selected_button_id,     // button ID
                MAIN_MENU_ID,          // 0x15
                0,                     // param_3 = 0
                1,                     // param_4 = 1  
                0,                     // param_5 = 0
                nullptr,               // param_6 (in_stack_ffffffe4)
                nullptr                // param_7 (in_stack_ffffffe8)
            );

            if (transitionResult != 0) {
                L.Get()->info("Transition approved, now changing menu state.");

                *(uint16_t*)((uintptr_t)pButtonContainer + 0x1A) = selected_button_id;
                *(uint16_t*)((uintptr_t)pButtonContainer + 0x18) = 0;
                // Ora puoi cambiare lo stato
                ChangeMenuState(pButtonContainer, MODLOADER_MENU_ID);
                L.Get()->info("Changed menu state to ModLoaderMenu (0x{:X}).", MODLOADER_MENU_ID);

                // Potresti dover fare altre operazioni come nella funzione originale
                // FUN_00b7ab40(0x17); // Equivalente a qualche operazione di stato
                // TODO add the other functions

            }
            else {
                L.Get()->warn("Transition was not approved. Aborting state change.");
            }

            L.Get()->flush();
        }

        void* __fastcall HookedAllocateData(void* this_ptr, void* edx_dummy, uint32_t size) {
            if (!g_pMemoryManager) {
                g_pMemoryManager = this_ptr;
                L.Get()->info(">>> Game Memory Manager captured at: {:p}", g_pMemoryManager);
            }
            return g_originalAllocateData(this_ptr, size);
        }

        void __fastcall HookedMainMenuSceneConstructor(void* this_ptr, void* edx_dummy, void* pSceneObject) {
            L.Get()->debug("HookedMainMenuSceneConstructor entered. Calling original function first. Scene this_ptr: {:p}, pSceneObject: {:p}", this_ptr, pSceneObject);
            g_originalMainMenuSceneConstructor(this_ptr, pSceneObject);

            if (!g_pMemoryManager) {
                L.Get()->error("Injection FAILED: Memory Manager not captured. Aborting.");
                return;
            }
            L.Get()->debug("Memory Manager is available at {:p}", g_pMemoryManager);

            /*if (!g_modloaderMenuLoaded && PrepareStringObject && LoadSymbol) {
                L.Get()->debug("Attempting to load 'modloadermenu' symbol.");

                // DEBUG: Controlla il puntatore che causa il crash
                uintptr_t val_at_0x5c = 0;
                if ((uintptr_t)pSceneObject + 0x5c < 0x1000) { // Semplice check per puntatori molto bassi (probabile nullptr/invalid)
                    L.Get()->error("pSceneObject is likely invalid or too low for offset 0x5c: {:p}", pSceneObject);
                }
                else {
                    val_at_0x5c = *(uintptr_t*)((uintptr_t)pSceneObject + 0x5c);
                    L.Get()->debug("Value at pSceneObject + 0x5c: {:p}", (void*)val_at_0x5c);
                }

                if (val_at_0x5c == 0) {
                    L.Get()->error("pSceneObject + 0x5c is NULL. Cannot load symbol 'modloadermenu' yet. Skipping this time.");
                    L.Get()->flush();
                    // Non impostare g_modloaderMenuLoaded a true, riproveremo al prossimo ciclo
                }
                else {
                    uint8_t tempStringObj[0x40];
                    PrepareStringObject(tempStringObj, "modloadermenu");
                    L.Get()->debug("Prepared internal string for 'modloadermenu'.");

                    // Chiamiamo LoadSymbol. Il secondo argomento deve essere l'oggetto scena corretto.
                    // Secondo l'analisi, è pSceneObject.
                    void* pModLoaderMenu = LoadSymbol(g_pMemoryManager, this_ptr, tempStringObj, 0x7e2, 0.0f, 0.0f);

                    if (pModLoaderMenu) {
                        g_myCustomSceneObject = pModLoaderMenu;
                        L.Get()->info("Successfully loaded 'modloadermenu' at: {:p}", g_myCustomSceneObject);

                        // Memorizza il puntatore al nostro menu nell'oggetto scena principale.
                        *(void**)((uintptr_t)this_ptr + MODLOADER_MENU_PTR_OFFSET_IN_SCENE) = g_myCustomSceneObject;
                        L.Get()->info("Stored 'modloadermenu' pointer at scene object (MainMenuScene instance) + 0x{:X}", MODLOADER_MENU_PTR_OFFSET_IN_SCENE);

                        // Imposta il menu come invisibile di default (offset 0x2f per il flag visibilità)
                        *(uint8_t*)((uintptr_t)g_myCustomSceneObject + 0x2f) = 0;
                        L.Get()->info("Set 'modloadermenu' to invisible initially.");
                        g_modloaderMenuLoaded = true; // Simbolo caricato con successo
                    }
                    else {
                        L.Get()->error("FAILED to load 'modloadermenu'. pModLoaderMenu is null.");
                    }
                }
            }
            else if (g_modloaderMenuLoaded) {
                L.Get()->debug("'modloadermenu' already loaded. Skipping.");
            }
            else {
                L.Get()->error("Injection FAILED: PrepareStringObject or LoadSymbol not resolved. Aborting 'modloadermenu' creation.");
            }*/

            void* myButtonVisualObject = AllocateData(g_pMemoryManager, 0x50);
            if (!myButtonVisualObject) {
                L.Get()->error("  -> FAILED to allocate memory for visual button object. Aborting.");
                return;
            }
            CreateButtonObject(myButtonVisualObject, this_ptr, reinterpret_cast<void*>(&MyCustomButton_ActionCallback), 0, 0, 0);
            *(uint16_t*)((uintptr_t)myButtonVisualObject + 0x48) = 1;
            FinalizeAndAddElement(pSceneObject, myButtonVisualObject);

            void* pMyButtonLogicalData = AllocateData(g_pMemoryManager, 0x34);
            if (!pMyButtonLogicalData) {
                L.Get()->error("  -> FAILED to allocate memory for logical button data. Aborting.");
                return;
            }

            memset(pMyButtonLogicalData, 0, 0x34);

            uint32_t* puVar4 = (uint32_t*)pMyButtonLogicalData;

            *(uint8_t*)((uintptr_t)puVar4 + 0x30) |= 1;

            const float original_x_start = 25.5f;
            const float original_y_start = 91.5f;
            const float original_x_end = 279.0f;
            const float original_y_end = 144.5f;

            const float horizontal_shift = 255.0f;

            float new_x_start = original_x_start + horizontal_shift;
            float new_y_start = original_y_start;
            float new_x_end = original_x_end + horizontal_shift;
            float new_y_end = original_y_end;

            puVar4[0] = *reinterpret_cast<uint32_t*>(&new_x_start);
            puVar4[1] = *reinterpret_cast<uint32_t*>(&new_y_start);
            puVar4[2] = *reinterpret_cast<uint32_t*>(&new_x_end);
            puVar4[3] = *reinterpret_cast<uint32_t*>(&new_y_end);

            puVar4[7] = reinterpret_cast<uint32_t>(this_ptr);
            puVar4[8] = reinterpret_cast<uint32_t>(&MyCustomButton_Select);
            puVar4[9] = 0;
            puVar4[10] = reinterpret_cast<uint32_t>(g_buttonDispatcherFunc);

            uintptr_t sceneAddr = reinterpret_cast<uintptr_t>(pSceneObject);
            uint16_t* pListCounter = (uint16_t*)(sceneAddr + 0x0A);
            void** pButtonList = (void**)(sceneAddr + 0x1C);

            if (*pListCounter < 40) {
                pButtonList[*pListCounter] = pMyButtonLogicalData;
                *pListCounter += 1;
            }
        }

        bool SetupMainMenuSystemHook(uintptr_t moduleBase) {
            CreateButtonObject = reinterpret_cast<CreateButtonObject_t>(moduleBase + CREATE_BUTTON_OFFSET);
            AllocateData = reinterpret_cast<AllocateData_t>(moduleBase + ALLOCATE_DATA_OFFSET);
            FinalizeAndAddElement = reinterpret_cast<FinalizeAndAddElement_t>(moduleBase + FINALIZE_AND_ADD_ELEMENT_OFFSET);
            g_buttonDispatcherFunc = (void*)(moduleBase + BUTTON_DISPATCHER_OFFSET);
            PrepareStringObject = reinterpret_cast<PrepareStringObject_t>(moduleBase + PREPARE_STRING_OBJECT_OFFSET);
            LoadSymbol = reinterpret_cast<LoadSymbol_t>(moduleBase + LOAD_SYMBOL_OFFSET);
            ChangeMenuState = reinterpret_cast<ChangeMenuState_t>(moduleBase + CHANGE_MENU_STATE_OFFSET);
            TransitionControl = reinterpret_cast<TransitionControl_t>(moduleBase + TRANSITION_CONTROL_OFFSET);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            g_originalAllocateData = reinterpret_cast<AllocateData_t>(moduleBase + ALLOCATE_DATA_OFFSET);
            LONG error = DetourAttach(&(PVOID&)g_originalAllocateData, HookedAllocateData);
            if (error != NO_ERROR) {
                L.Get()->error("DetourAttach for AllocateData failed: {}", error);
                DetourTransactionAbort(); return false;
            }

            g_originalMainMenuSceneConstructor = reinterpret_cast<MainMenuSceneConstructor_t>(moduleBase + MAIN_MENU_SCENE_CONSTRUCTOR_OFFSET);
            error = DetourAttach(&(PVOID&)g_originalMainMenuSceneConstructor, HookedMainMenuSceneConstructor);
            if (error != NO_ERROR) {
                L.Get()->error("DetourAttach for MainMenuSceneConstructor failed: {}", error);
                DetourTransactionAbort(); return false;
            }

            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("DetourTransactionCommit failed! Error: {}", error);
                return false;
            }

            L.Get()->info("--- All MainMenuSystem hooks attached successfully. ---");
            L.Get()->flush();
            return true;
        }
    }
}