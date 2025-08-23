#include "NativeFunctions.h"

namespace HookCrashers {
    namespace Native {
        namespace Natives {
            // Definizione dei nativi
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
        }

        bool LoadNatives(uintptr_t moduleBase) {
            // Carica tutti gli indirizzi basati su moduleBase
            Natives::IsCharacterAvailableInGameMode.Address = reinterpret_cast<void*>(moduleBase + 0x8DC00); // TODO patch < 49
            Natives::GetSpecialCharacterIdForTeam.Address = reinterpret_cast<void*>(moduleBase + 0x8DBD0); // TODO patch
            Natives::IsInCharSelect.Address = reinterpret_cast<void*>(moduleBase + 0xE5BB0);
            Natives::IsCharDLC.Address = reinterpret_cast<void*>(moduleBase + 0x8DC90);
            Natives::IsLocalMultiplayer.Address = reinterpret_cast<void*>(moduleBase + 0x82120);
            Natives::GetPlayerOnlineId.Address = reinterpret_cast<void*>(moduleBase + 0xEE210); // 0xb4e180 - 0xa60000
            Natives::UpdatePlayerIconState.Address = reinterpret_cast<void*>(moduleBase + 0x107720); // 0xb67690 - 0xa60000
            Natives::IsBaseCharAvailableInMode.Address = reinterpret_cast<void*>(moduleBase + 0x8DDA0); // TODO Patch
            Natives::Native_Func_AE4AC0.Address = reinterpret_cast<void*>(moduleBase + 0x84AE0); // 0xae4ac0 - 0xa60000
            Natives::IsWorkshopCharAvailableInMode.Address = reinterpret_cast<void*>(moduleBase + 0x8DD60); // TODO Patch
            Natives::Native_Func_AEDDA0.Address = reinterpret_cast<void*>(moduleBase + 0x8DE00); // TODO Patch
            Natives::GetTeamId.Address = reinterpret_cast<void*>(moduleBase + 0x891B0); // 0xae91a0 - 0xa60000
            Natives::IsTeamReady.Address = reinterpret_cast<void*>(moduleBase + 0x89180); // 0xae9170 - 0xa60000
            Natives::FinalizeTeamSelection.Address = reinterpret_cast<void*>(moduleBase + 0x8DFC0); // TODO patch

            // Verifica che almeno le funzioni critiche siano caricate
            return true;
        }
    }
}