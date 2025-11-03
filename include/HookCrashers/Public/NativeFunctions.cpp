#include "NativeFunctions.h"

namespace HookCrashers {
    namespace Native {
        namespace Natives {
            // Definizione e inizializzazione delle variabili NativeInfo
            HOOKCRASHERS_API NativeInfo<void*> IsCharacterAvailableInGameMode = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> GetSpecialCharacterIdForTeam = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> IsInCharSelect = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> IsDLCOwned = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> IsLocalMultiplayer = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> GetPlayerOnlineId = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> UpdatePlayerIconState = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> IsBaseCharAvailableInMode = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> Native_Func_AE4AC0 = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> IsWorkshopCharAvailableInMode = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> Native_Func_AEDDA0 = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> GetTeamId = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> IsTeamReady = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> FinalizeTeamSelection = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> IsUpPressed = { nullptr, CallingConvention::ThisCall };
            HOOKCRASHERS_API NativeInfo<void*> IsDownPressed = { nullptr, CallingConvention::ThisCall };
            HOOKCRASHERS_API NativeInfo<void*> IsLeftPressed = { nullptr, CallingConvention::ThisCall };
            HOOKCRASHERS_API NativeInfo<void*> IsRightPressed = { nullptr, CallingConvention::ThisCall };
            HOOKCRASHERS_API NativeInfo<void*> IsEnterPressed = { nullptr, CallingConvention::ThisCall };
            HOOKCRASHERS_API NativeInfo<void*> RefreshGui = { nullptr, CallingConvention::StdCall };
            HOOKCRASHERS_API NativeInfo<void*> GotoAndStop = { nullptr, CallingConvention::ThisCall };
            HOOKCRASHERS_API NativeInfo<void*> DecryptSaveFile = { nullptr, CallingConvention::ThisCall };
        }

        // Definizione (implementazione) della funzione di inizializzazione
        bool LoadNatives(uintptr_t moduleBase) {
            Natives::IsCharacterAvailableInGameMode.Address = reinterpret_cast<void*>(moduleBase + 0x8DC00);
            Natives::GetSpecialCharacterIdForTeam.Address = reinterpret_cast<void*>(moduleBase + 0x8E750);
            Natives::IsInCharSelect.Address = reinterpret_cast<void*>(moduleBase + 0xE6470);
            Natives::IsDLCOwned.Address = reinterpret_cast<void*>(moduleBase + 0x8E7B0);
            Natives::IsLocalMultiplayer.Address = reinterpret_cast<void*>(moduleBase + 0x82C40);
            Natives::GetPlayerOnlineId.Address = reinterpret_cast<void*>(moduleBase + 0xEEAB0);
            Natives::UpdatePlayerIconState.Address = reinterpret_cast<void*>(moduleBase + 0x108010);
            Natives::IsBaseCharAvailableInMode.Address = reinterpret_cast<void*>(moduleBase + 0x8E8C0);
            Natives::Native_Func_AE4AC0.Address = reinterpret_cast<void*>(moduleBase + 0x85600);
            Natives::IsWorkshopCharAvailableInMode.Address = reinterpret_cast<void*>(moduleBase + 0x8E880);
            Natives::Native_Func_AEDDA0.Address = reinterpret_cast<void*>(moduleBase + 0x8E920);
            Natives::GetTeamId.Address = reinterpret_cast<void*>(moduleBase + 0x89CD0);
            Natives::IsTeamReady.Address = reinterpret_cast<void*>(moduleBase + 0x89CA0);
            Natives::FinalizeTeamSelection.Address = reinterpret_cast<void*>(moduleBase + 0x8EAE0);
            Natives::IsRightPressed.Address = reinterpret_cast<void*>(moduleBase + 0x14F4F0);
            Natives::IsLeftPressed.Address = reinterpret_cast<void*>(moduleBase + 0x14F340);
            Natives::IsUpPressed.Address = reinterpret_cast<void*>(moduleBase + 0x14F190);
            Natives::IsDownPressed.Address = reinterpret_cast<void*>(moduleBase + 0x14EFE0);
            Natives::IsEnterPressed.Address = reinterpret_cast<void*>(moduleBase + 0x14F6A0);
            Natives::RefreshGui.Address = reinterpret_cast<void*>(moduleBase + 0xC7D30);
            Natives::GotoAndStop.Address = reinterpret_cast<void*>(moduleBase + 0x1180D0);
			Natives::DecryptSaveFile.Address = reinterpret_cast<void*>(moduleBase + 0xDBD20);
            return true;
        }
    }
}