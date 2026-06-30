// Hooks/GenerateDefaultCharacterDataHook.cpp
#include "GenerateDefaultCharacterDataHook.h"
#include "CharacterConfig.h"
#include <windows.h>
#include <detours.h>
#include "../Util/Logger.h"
#include "../../include/HookCrashers/Public/Globals.h"


namespace HookCrashers::Save {

    static const uintptr_t GENERATE_DEFAULT_CHAR_DATA = 0x12FCD0; // updated

    using OriginalGenerateDefaultCharacterData_t = unsigned int(__fastcall*)(void* a1, int characterIndex);
    static OriginalGenerateDefaultCharacterData_t g_originalFunction = nullptr;

    unsigned char GetStartingWeaponId(int index) {
        switch (index) {
        case 0: return 3;
        case 1: return 25;
        case 2: return 39;
        case 3: return 56;
        case 4: case 22: return 2;
        case 5: return 19;
        case 6: return 10;
        case 7: return 18;
        case 8: return 34;
        case 9: return 27;
        case 10: return 47;
        case 11: return 36;
        case 12: return 12;
        case 13: return 33;
        case 14: case 15: return 15;
        case 16: return 6;
        case 17: return 45;
        case 18: return 26;
        case 19: return 57;
        case 20: return 43;
        case 21: return 20;
        case 23: return 35;
        case 24: return 31;
        case 25: return 48;
        case 26: return 50;
        case 27: return 68;
        case 28: return 63;
        case 29: return 85;
        case 30: return 84;
        case 31: return 86;
        }
        int addonCount = CharacterConfig::Instance().GetAddonCount();

        if (index >= BASE_CHAR_COUNT && index < BASE_CHAR_COUNT + addonCount)
        {
            const auto& addon = CharacterConfig::Instance().GetAddons()[index - BASE_CHAR_COUNT];
            return addon.weapon;
        }
        
        if ((unsigned short)(index - BASE_CHAR_COUNT - addonCount) <= 9)
            return 214;

        return 0;
    }

    unsigned int __fastcall DetouredGenerateDefaultCharacterData(void* manager, int characterIndex) {
        void* pManager = manager;
        unsigned int uIdx = (unsigned int)(unsigned short)characterIndex;

        unsigned int* ctx = (unsigned int*)pManager;
        unsigned char* buffer = (unsigned char*)ctx[1];
        unsigned int offset = (48 * uIdx) + 64;
        unsigned char* pBlock = buffer + offset;

        pBlock[1] = 0;
        pBlock[6] = GetStartingWeaponId(uIdx);
        pBlock[8] = 1;
        pBlock[9] = 1;
        pBlock[10] = 1;
        pBlock[11] = 1;

        ctx[3] = offset + 46;

        unsigned int returnAddress = (unsigned int)pBlock;

        __asm {
            mov ecx, pManager
            mov eax, returnAddress
        }
    }

    bool SetupGenerateDefaultCharacterDataHook() {
        uintptr_t base = g_moduleBase;
        uintptr_t targetAddress = base + GENERATE_DEFAULT_CHAR_DATA;

        g_originalFunction = reinterpret_cast<OriginalGenerateDefaultCharacterData_t>(targetAddress);

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        if (DetourAttach(&(PVOID&)g_originalFunction, DetouredGenerateDefaultCharacterData) != NO_ERROR) {
            HookCrashers::Util::Logger::Instance().Get()->error("[GenerateDefaultCharData] DetourAttach FAILED");
            DetourTransactionAbort();
            return false;
        }
        if (DetourTransactionCommit() != NO_ERROR) {
            HookCrashers::Util::Logger::Instance().Get()->error("[GenerateDefaultCharData] DetourTransactionCommit FAILED");
            return false;
        }

        HookCrashers::Util::Logger::Instance().Get()->debug("[GenerateDefaultCharData] Hook attached successfully!");
        return true;
    }

} // namespace HookCrashers::Save
