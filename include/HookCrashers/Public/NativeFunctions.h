#pragma once
#include "NativeCaller.h"

#ifndef HOOKCRASHERS_API
    #ifdef HOOKCRASHERS_EXPORTS
        #define HOOKCRASHERS_API __declspec(dllexport)
    #else
        #define HOOKCRASHERS_API __declspec(dllimport)
    #endif
#endif

namespace HookCrashers {
    namespace Native {
        namespace Natives {
            // Definisci tutti i nativi che usi
            extern HOOKCRASHERS_API NativeInfo<void*> IsCharacterAvailableInGameMode;
            extern HOOKCRASHERS_API NativeInfo<void*> GetSpecialCharacterIdForTeam;
            extern HOOKCRASHERS_API NativeInfo<void*> IsInCharSelect;
            extern HOOKCRASHERS_API NativeInfo<void*> IsCharDLC;
            extern HOOKCRASHERS_API NativeInfo<void*> IsLocalMultiplayer;
            extern HOOKCRASHERS_API NativeInfo<void*> GetPlayerOnlineId; // FUN_00b4e180
            extern HOOKCRASHERS_API NativeInfo<void*> UpdatePlayerIconState; // FUN_00b67690
            extern HOOKCRASHERS_API NativeInfo<void*> IsBaseCharAvailableInMode; // FUN_00aedd40
            extern HOOKCRASHERS_API NativeInfo<void*> Native_Func_AE4AC0; // FUN_00ae4ac0 (Nome generico)
            extern HOOKCRASHERS_API NativeInfo<void*> IsWorkshopCharAvailableInMode; // FUN_00aedd00
            extern HOOKCRASHERS_API NativeInfo<void*> Native_Func_AEDDA0; // FUN_00aedda0
            extern HOOKCRASHERS_API NativeInfo<void*> GetTeamId; // FUN_00ae91a0
            extern HOOKCRASHERS_API NativeInfo<void*> IsTeamReady; // FUN_00ae9170
            extern HOOKCRASHERS_API NativeInfo<void*> FinalizeTeamSelection; // FUN_00aedf60
        }

        // Funzione per inizializzare tutti i nativi
        bool LoadNatives(uintptr_t moduleBase);
    }
}