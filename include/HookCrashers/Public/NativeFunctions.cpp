#include "NativeFunctions.h"

namespace HookCrashers {
    namespace Native {
        namespace Natives {
            // Definizione e inizializzazione delle variabili NativeInfo
            HOOKCRASHERS_API NativeInfo<void*> IsCharacterAvailableInGameMode = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> GetSpecialCharacterIdForTeam = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> IsInCharSelect = { nullptr, CallingConvention::Cdecl };
            HOOKCRASHERS_API NativeInfo<void*> IsCharDLC = { nullptr, CallingConvention::Cdecl };
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
            Natives::GetSpecialCharacterIdForTeam.Address = reinterpret_cast<void*>(moduleBase + 0x8DBD0);
            Natives::IsInCharSelect.Address = reinterpret_cast<void*>(moduleBase + 0xE5BB0);
            Natives::IsCharDLC.Address = reinterpret_cast<void*>(moduleBase + 0x8DC90);
            Natives::IsLocalMultiplayer.Address = reinterpret_cast<void*>(moduleBase + 0x82120);
            Natives::GetPlayerOnlineId.Address = reinterpret_cast<void*>(moduleBase + 0xEE210);
            Natives::UpdatePlayerIconState.Address = reinterpret_cast<void*>(moduleBase + 0x107720);
            Natives::IsBaseCharAvailableInMode.Address = reinterpret_cast<void*>(moduleBase + 0x8DDA0);
            Natives::Native_Func_AE4AC0.Address = reinterpret_cast<void*>(moduleBase + 0x84AE0);
            Natives::IsWorkshopCharAvailableInMode.Address = reinterpret_cast<void*>(moduleBase + 0x8DD60);
            Natives::Native_Func_AEDDA0.Address = reinterpret_cast<void*>(moduleBase + 0x8DE00);
            Natives::GetTeamId.Address = reinterpret_cast<void*>(moduleBase + 0x891B0);
            Natives::IsTeamReady.Address = reinterpret_cast<void*>(moduleBase + 0x89180);
            Natives::FinalizeTeamSelection.Address = reinterpret_cast<void*>(moduleBase + 0x8DFC0);
            Natives::IsRightPressed.Address = reinterpret_cast<void*>(moduleBase + 0x14EB90);
            Natives::IsLeftPressed.Address = reinterpret_cast<void*>(moduleBase + 0x14E9E0);
            Natives::IsUpPressed.Address = reinterpret_cast<void*>(moduleBase + 0x14E830);
            Natives::IsDownPressed.Address = reinterpret_cast<void*>(moduleBase + 0x14E680);
            Natives::IsEnterPressed.Address = reinterpret_cast<void*>(moduleBase + 0x14ED40);
            Natives::RefreshGui.Address = reinterpret_cast<void*>(moduleBase + 0xC74A0);
            Natives::GotoAndStop.Address = reinterpret_cast<void*>(moduleBase + 0x1177E0);
			Natives::DecryptSaveFile.Address = reinterpret_cast<void*>(moduleBase + 0xDB470);
            return true;
        }
    }
}