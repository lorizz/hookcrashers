#include "InitCharDataTableHook.h"
#include <detours.h>
#include <windows.h>
#include "../SaveData/SaveDataManager.h"

namespace HookCrashers {
    constexpr uintptr_t InitCharDataTable_OFFSET = 0x83C10;
    constexpr uintptr_t CHAR_DATA_TABLE_OFFSET = 0x11A8;

    using OriginalInitCharDataTable_t = void(__thiscall*)(void* thisPtr, char param1);
    static OriginalInitCharDataTable_t g_originalFunction = nullptr;

    // Cache per lo stato "perfetto" della tabella
    static std::vector<void*> g_cachedTableState;
    static bool g_isStateCached = false;

    void __fastcall DetouredInitCharDataTable(void* thisPtr, void* edx, char param1) {

        // --- CASO 1: Modalità "UI Stabile" (param1 == 0) ---
        // Se abbiamo uno stato salvato, lo usiamo per forzare la tabella e prevenire corruzione.
        if (param1 == 0) {
            if (g_isStateCached) {
                void** charDataTable = (void**)((char*)thisPtr + CHAR_DATA_TABLE_OFFSET);
                memcpy(charDataTable, g_cachedTableState.data(), g_cachedTableState.size() * sizeof(void*));
                // Non chiamiamo la funzione originale.
                return;
            }
        }

        // --- CASO 2: Modalità "Reset e Ricarica" (param1 == 1) ---
        // Questa è la chiamata che popola la tabella con i dati corretti.
        if (param1 == 1) {
            // 1. Resetta lo stato precedente
            g_isStateCached = false;
            g_cachedTableState.clear();

            // 2. Esegui la funzione originale per popolare la tabella
            if (g_originalFunction) {
                g_originalFunction(thisPtr, param1);
            }

            // 3. Salva il nuovo stato "perfetto"
            void** charDataTable = (void**)((char*)thisPtr + CHAR_DATA_TABLE_OFFSET);
            const size_t tableSize = SaveDataManager::TOTAL_STREAMED_CHARACTERS;
            g_cachedTableState.assign(charDataTable, charDataTable + tableSize);
            g_isStateCached = true;

            // Abbiamo finito per questa chiamata.
            return;
        }

        // --- Fallback per qualsiasi altro valore di param1 ---
        // Se param1 ha un valore inaspettato, eseguiamo semplicemente la funzione originale.
        if (g_originalFunction) {
            g_originalFunction(thisPtr, param1);
        }
    }

    bool SetupInitCharDataTableHook(uintptr_t moduleBase) {

        uintptr_t targetAddress = moduleBase + InitCharDataTable_OFFSET;
        g_originalFunction = reinterpret_cast<OriginalInitCharDataTable_t>(targetAddress);

        if (!g_originalFunction) {
            return false;
        }

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (DetourAttach(&(PVOID&)g_originalFunction, DetouredInitCharDataTable) != NO_ERROR) {
            DetourTransactionAbort();
            return false;
        }

        if (DetourTransactionCommit() != NO_ERROR) {
            return false;
        }

        return true;
    }
}