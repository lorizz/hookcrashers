#pragma once

#include "../stdafx.h"

namespace HookCrashers {
    namespace Core {
        bool SetupGetPlayerObjectHook(uintptr_t moduleBase);

        void* __fastcall DetouredGetPlayerObject(void* thisPtr, void* /* edx */, uint16_t playerId);

        void* GetPlayerObject(uint16_t playerId);
        char GetPlayerState(void* playerObj);
        char GetPlayerActiveState(void* playerObj);
        uint64_t GetPlayerPosition(void* playerObj);
        int GetPlayerSelectedCharacterType(void* playerObj);
        bool IsOnlineMode();

        extern uintptr_t* g_pGameManagerPtr;
    }
}
