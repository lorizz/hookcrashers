#pragma once
#include "NativeCaller.h"

namespace HookCrashers {
    namespace Native {
        namespace Natives {
            // Definisci tutti i nativi che usi
            extern NativeInfo<void*> IsCharacterAvailableInGameMode;
            extern NativeInfo<void*> GetSpecialCharacterIdForTeam;
            extern NativeInfo<void*> IsInCharSelect;
            extern NativeInfo<void*> IsCharDLC;
            extern NativeInfo<void*> IsLocalMultiplayer;
            extern NativeInfo<void*> GetPlayerOnlineId; // FUN_00b4e180
            extern NativeInfo<void*> UpdatePlayerIconState; // FUN_00b67690
            extern NativeInfo<void*> IsBaseCharAvailableInMode; // FUN_00aedd40
            extern NativeInfo<void*> Native_Func_AE4AC0; // FUN_00ae4ac0 (Nome generico)
            extern NativeInfo<void*> IsWorkshopCharAvailableInMode; // FUN_00aedd00
            extern NativeInfo<void*> Native_Func_AEDDA0; // FUN_00aedda0
            extern NativeInfo<void*> GetTeamId; // FUN_00ae91a0
            extern NativeInfo<void*> IsTeamReady; // FUN_00ae9170
            extern NativeInfo<void*> FinalizeTeamSelection; // FUN_00aedf60
        }

        // Funzione per inizializzare tutti i nativi
        bool LoadNatives(uintptr_t moduleBase);
    }
}