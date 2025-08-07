// SceneConstructorHook.cpp - VERSIONE CORRETTA
#include "SceneConstructorHook.h"
#include "MainMenuSystemHook.h"
#include "../Util/Logger.h"
#include <detours.h>
#include <windows.h>

namespace HookCrashers {
    namespace Core {
        static Util::Logger& L = Util::Logger::Instance();

        // Dichiarazione della variabile esterna dal MainMenuSystemHook
        extern void* g_pMemoryManager;
        extern AllocateData_t AllocateData;

        // Puntatore globale al nostro oggetto di menu una volta caricato
        void* g_pModLoaderMenuObject = nullptr;
        void* g_pMyBigDataBlock = nullptr;

        // Tipi di funzione necessari, basati sulla tua analisi e gli offset
        using SceneConstructor_t = void* (__thiscall*)(void* this_ptr, int param_1);
        using LoadSymbol_t = void* (__thiscall*)(void* pMemoryManager, void* pSceneObject, void* pStringObject, uint32_t someID, float x, float y);
        using PrepareStringObject_t = char* (__thiscall*)(void* pStringObjectBuffer, const char* c_string);
        using FinalizeAndAddElement_t = void(__thiscall*)(void* pHandlerData, void* pElement);

        // Puntatori alle funzioni originali e del gioco
        static SceneConstructor_t g_originalSceneConstructor = nullptr;
        static LoadSymbol_t LoadSymbol = nullptr;
        static PrepareStringObject_t PrepareStringObject = nullptr;
        static FinalizeAndAddElement_t FinalizeAndAddElement = nullptr;

        // Offset dal tuo codice
        constexpr uintptr_t SCENE_CONSTRUCTOR_OFFSET = 0x9F740;
        constexpr uintptr_t LOAD_SYMBOL_OFFSET = 0xAB5C0;
        constexpr uintptr_t PREPARE_STRING_OBJECT_OFFSET = 0x221B0;
        constexpr uintptr_t FINALIZE_AND_ADD_ELEMENT_OFFSET = 0xA8610;

        constexpr uint16_t MODLOADER_MENU_ID = 0x28; // Usiamo l'indice 40
        constexpr size_t HANDLER_SIZE = 0x20C;
        constexpr size_t ORIGINAL_HANDLER_COUNT = 40; // 0x28
        constexpr size_t NEW_HANDLER_COUNT = 41;


        /// <summary>
        /// Handler personalizzato che carica il simbolo "modloadermenu".
        /// </summary>
        void __fastcall ModLoaderMenuHandler(void* this_scene, void* edx_dummy, void* pHandlerData)
        {
            L.Get()->debug("ModLoaderMenuHandler executed.");

            if (!g_pMemoryManager || !AllocateData) {
                L.Get()->error("Memory Manager or AllocateData not available for ModLoaderMenuHandler. Aborting.");
                return;
            }

            void* pSceneObject = *(void**)((uintptr_t)this_scene + 4);

            // 1. Alloca la memoria per l'oggetto simbolo
            const uint32_t symbolObjectSize = 0x58;
            void* pMemoryForSymbol = AllocateData(g_pMemoryManager, symbolObjectSize);

            if (!pMemoryForSymbol) {
                L.Get()->error("Failed to allocate 0x{:X} bytes of memory for the symbol object.", symbolObjectSize);
                return;
            }
            L.Get()->info("Allocated memory for symbol object at: {:p}", pMemoryForSymbol);

            // 2. Prepara la stringa del simbolo
            uint8_t tempStringObj[0x40];
            PrepareStringObject(tempStringObj, "modloadermenu");

            // 3. Chiama LoadSymbol
            void* pModLoaderMenu = LoadSymbol(pMemoryForSymbol, pSceneObject, tempStringObj, 0x7E2, 0.0f, 0.0f);

            if (pModLoaderMenu) {
                g_pModLoaderMenuObject = pModLoaderMenu;
                L.Get()->info("Successfully constructed 'modloadermenu' symbol at: {:p}", g_pModLoaderMenuObject);

                *(uint8_t*)((uintptr_t)g_pModLoaderMenuObject + 0x2F) = 0;
                L.Get()->info("Set 'modloadermenu' to invisible.");

                FinalizeAndAddElement(pHandlerData, g_pModLoaderMenuObject);
                L.Get()->info("'modloadermenu' element finalized and added to its handler data at {:p}.", pHandlerData);
            }
            else {
                L.Get()->error("LoadSymbol returned null unexpectedly.");
            }
            L.Get()->flush();
        }

        /// <summary>
        /// La nostra funzione di detour per il costruttore della scena.
        /// </summary>
        void* __fastcall HookedSceneConstructor(void* this_ptr, void* edx_dummy, int param_1)
        {
            // 1. Chiama l'originale per far fare al gioco tutto il lavoro di setup.
            void* result = g_originalSceneConstructor(this_ptr, param_1);

            // A questo punto, il gioco ha creato il suo array di 40 handler.
            uintptr_t pOriginalBigDataBlock = *(uintptr_t*)((uintptr_t)this_ptr + 0x40);
            if (!pOriginalBigDataBlock) {
                L.Get()->error("Original Big Data Block is null. Aborting hook.");
                return result;
            }

            L.Get()->info("Original handler block is at: {:p}", (void*)pOriginalBigDataBlock);

            // 2. Alloca il NOSTRO nuovo blocco di memoria, più grande.
            const size_t newBlockSize = NEW_HANDLER_COUNT * HANDLER_SIZE;
            char* pNewBigDataBlock = new (std::nothrow) char[newBlockSize];
            if (!pNewBigDataBlock) {
                L.Get()->error("Failed to allocate our new, larger handler block.");
                return result;
            }
            L.Get()->info("Allocated new handler block of size 0x{:X} at: {:p}", newBlockSize, pNewBigDataBlock);

            // 3. Copia i 40 handler originali nel nostro nuovo blocco.
            const size_t originalBlockSize = ORIGINAL_HANDLER_COUNT * HANDLER_SIZE;
            memcpy(pNewBigDataBlock, (void*)pOriginalBigDataBlock, originalBlockSize);
            L.Get()->info("Copied original handlers to new block.");

            // 5. Prepara il nostro slot (l'ultimo, il 41°)
            void* pOurHandlerSlot = pNewBigDataBlock + originalBlockSize;

            // 6. Prendi un handler "template" dal blocco originale. 
            //    Quello a indice 0 va benissimo, è uno dei più semplici.
            void* pTemplateHandler = (void*)pOriginalBigDataBlock;

            // 7. Copia il template nel nostro slot.
            //    QUESTO È IL PASSAGGIO FONDAMENTALE. Copia i dati di base,
            //    incluso il PUNTATORE ALLA VTABLE (il primo dword).
            memcpy(pOurHandlerSlot, pTemplateHandler, HANDLER_SIZE);
            L.Get()->info("Cloned template handler into our new slot to initialize it with a valid VTable.");
            L.Get()->info("VTable for our handler (slot 0x{:X}) is now: 0x{:X}", MODLOADER_MENU_ID, *(uintptr_t*)pOurHandlerSlot);

            // ----------------------------

            // 8. Ora che il nostro slot è una copia valida di un handler esistente (con una vtable),
            //    chiamiamo il nostro ModLoaderMenuHandler per personalizzarlo (aggiungere il simbolo, ecc.).
            ModLoaderMenuHandler(this_ptr, nullptr, pOurHandlerSlot);

            // 9. Fai puntare il gioco al nostro nuovo blocco.
            *(uintptr_t*)((uintptr_t)this_ptr + 0x40) = (uintptr_t)pNewBigDataBlock;
            L.Get()->info("Redirected game to use our new handler block. This should be the final step.");

            return result;
        }

        bool SetupSceneConstructorHook(uintptr_t moduleBase)
        {
            // Risolvi gli indirizzi delle funzioni del gioco
            LoadSymbol = reinterpret_cast<LoadSymbol_t>(moduleBase + LOAD_SYMBOL_OFFSET);
            PrepareStringObject = reinterpret_cast<PrepareStringObject_t>(moduleBase + PREPARE_STRING_OBJECT_OFFSET);
            FinalizeAndAddElement = reinterpret_cast<FinalizeAndAddElement_t>(moduleBase + FINALIZE_AND_ADD_ELEMENT_OFFSET);

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            g_originalSceneConstructor = reinterpret_cast<SceneConstructor_t>(moduleBase + SCENE_CONSTRUCTOR_OFFSET);

            // Ora possiamo usare il main hook con il nuovo slot 0x28
            LONG error = DetourAttach(&(PVOID&)g_originalSceneConstructor, HookedSceneConstructor);

            if (error != NO_ERROR) {
                L.Get()->error("DetourAttach for SceneConstructor failed: {}", error);
                DetourTransactionAbort();
                return false;
            }

            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("DetourTransactionCommit for SceneConstructorHook failed! Error: {}", error);
                return false;
            }

            L.Get()->info("--- SceneConstructorHook attached successfully with NEW HANDLER SLOT 0x28! ---");
            L.Get()->flush();
            return true;
        }
    }
}