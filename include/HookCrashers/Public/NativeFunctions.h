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
            // Dichiarazioni (extern) delle variabili NativeInfo
            extern HOOKCRASHERS_API NativeInfo<void*> IsCharacterAvailableInGameMode;
            extern HOOKCRASHERS_API NativeInfo<void*> GetSpecialCharacterIdForTeam;
            extern HOOKCRASHERS_API NativeInfo<void*> IsInCharSelect;
            extern HOOKCRASHERS_API NativeInfo<void*> IsCharDLC;
            extern HOOKCRASHERS_API NativeInfo<void*> IsLocalMultiplayer;
            extern HOOKCRASHERS_API NativeInfo<void*> GetPlayerOnlineId;
            extern HOOKCRASHERS_API NativeInfo<void*> UpdatePlayerIconState;
            extern HOOKCRASHERS_API NativeInfo<void*> IsBaseCharAvailableInMode;
            extern HOOKCRASHERS_API NativeInfo<void*> Native_Func_AE4AC0;
            extern HOOKCRASHERS_API NativeInfo<void*> IsWorkshopCharAvailableInMode;
            extern HOOKCRASHERS_API NativeInfo<void*> Native_Func_AEDDA0;
            extern HOOKCRASHERS_API NativeInfo<void*> GetTeamId;
            extern HOOKCRASHERS_API NativeInfo<void*> IsTeamReady;
            extern HOOKCRASHERS_API NativeInfo<void*> FinalizeTeamSelection;
            extern HOOKCRASHERS_API NativeInfo<void*> IsUpPressed;
            extern HOOKCRASHERS_API NativeInfo<void*> IsDownPressed;
            extern HOOKCRASHERS_API NativeInfo<void*> IsLeftPressed;
            extern HOOKCRASHERS_API NativeInfo<void*> IsRightPressed;
            extern HOOKCRASHERS_API NativeInfo<void*> IsEnterPressed;
            extern HOOKCRASHERS_API NativeInfo<void*> RefreshGui;
            extern HOOKCRASHERS_API NativeInfo<void*> GotoAndStop;
            extern HOOKCRASHERS_API NativeInfo<void*> DecryptSaveFile;
        }

        // Dichiarazione della funzione di inizializzazione
        bool LoadNatives(uintptr_t moduleBase);
    }
}