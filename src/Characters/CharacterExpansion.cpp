#include "CharacterExpansion.h"

#include "CharacterRegistry.h"
#include "../Core/Logger.h"
#include "../Core/Memory.h"
#include "../Lobby/LobbyPortraitExpansion.h"
#include "../NativeDB/Classes.h"
#include "../NativeDB/Natives.h"
#include "../Network/CharacterSyncExpansion.h"
#include "../SWF/Cok6LoadHook.h"
#include <windows.h>
#include <detours.h>
#include <cstdint>

namespace HookCrashers::Characters {
namespace {

using CreateDefaultSaveFn = void(__fastcall*)(void* saveManager);
CreateDefaultSaveFn g_OriginalCreateDefaultSave = nullptr;

using SanitizeCharacterSaveSlotsFn = std::uintptr_t(__fastcall*)(void* saveManager);
SanitizeCharacterSaveSlotsFn g_OriginalSanitizeCharacterSaveSlots = nullptr;

std::uint8_t VanillaStartingWeapon(std::uint32_t index) {
    switch (index) {
    case 0: return 3;
    case 1: return 25;
    case 2: return 39;
    case 3: return 56;
    case 4:
    case 22: return 2;
    case 5: return 19;
    case 6: return 10;
    case 7: return 18;
    case 8: return 34;
    case 9: return 27;
    case 10: return 47;
    case 11: return 36;
    case 12: return 12;
    case 13: return 33;
    case 14:
    case 15: return 15;
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
    default: return 0;
    }
}

void InitializeCharacterData(NativeDB::CC_CharacterData& character, std::uint32_t index) {
    character = {};
    character.level = 0;
    character.strength = 1;
    character.defense = 1;
    character.magic = 1;
    character.agility = 1;

    const Registry& registry = Registry::Instance();
    if (const AddonCharacter* addon = registry.AddonByBaseIndex(index)) {
        character.unlocked = addon->initiallyUnlocked ? NativeDB::kCharacterUnlockedFlag : 0;
        character.weapon = addon->startingWeapon;
        character.animal = addon->startingPet;
        return;
    }

    character.weapon = VanillaStartingWeapon(index);
    if (index >= static_cast<std::uint32_t>(registry.NetworkBaseCount()) &&
        index < static_cast<std::uint32_t>(registry.NetworkBaseCount() + NativeDB::kWorkshopSaveSlotCount)) {
        character.weapon = 214;
    }
}

void InitializeExpandedDefaultCharacterData(void* saveBuffer) {
    if (!saveBuffer) {
        Core::Logger::Error("InitializeExpandedDefaultCharacterData called with null save buffer");
        return;
    }

    auto* bytes = reinterpret_cast<std::uint8_t*>(saveBuffer);
    const Registry& registry = Registry::Instance();
    Core::Logger::Info("Initializing expanded default character data: saveSlots=%d addonCount=%d",
                       registry.SaveCharacterSlotCount(),
                       registry.AddonCount());
    for (int slot = 0; slot < registry.SaveCharacterSlotCount(); ++slot) {
        auto* character = reinterpret_cast<NativeDB::CC_CharacterData*>(
            bytes + NativeDB::kGlobalDataSize + NativeDB::kCharacterDataSize * slot);
        if (slot >= static_cast<int>(NativeDB::kVanillaBaseCharacterCount)) {
            InitializeCharacterData(*character, static_cast<std::uint32_t>(slot));
        }
    }
}

template <typename T, std::size_t N>
bool AnyNonZero(const std::array<T, N>& values) {
    for (const auto value : values) {
        if (value != 0) {
            return true;
        }
    }
    return false;
}

bool HasAddonProgress(const NativeDB::CC_CharacterData& character) {
    if (character.level != 0) {
        return true;
    }
    if (AnyNonZero(character.experience)) {
        return true;
    }
    return character.skull != 0 ||
           character.insane_mode != 0 ||
           character.potions != 0 ||
           character.bombs != 0 ||
           character.beefies != 0 ||
           AnyNonZero(character.gold) ||
           AnyNonZero(character.normal_levels_bitflags) ||
           AnyNonZero(character.insane_levels_bitflags) ||
           AnyNonZero(character.arena_wins) ||
           character.arena_losses != 0 ||
           character.arena_points != 0;
}

void RepairExpandedCharacterStatBaselines(void* saveManager) {
    if (!saveManager) {
        Core::Logger::Warn("Cannot repair addon stat baselines: save manager is null");
        return;
    }

    auto* saveBuffer = reinterpret_cast<std::uint8_t**>(saveManager)[1];
    if (!saveBuffer) {
        Core::Logger::Warn("Cannot repair addon stat baselines: save buffer is null");
        return;
    }

    const Registry& registry = Registry::Instance();
    int repairedSlots = 0;
    int repairedUnlocks = 0;
    for (int addon = 0; addon < registry.AddonCount(); ++addon) {
        const int slot = registry.FirstAddonIndex() + addon;
        auto* character = reinterpret_cast<NativeDB::CC_CharacterData*>(
            saveBuffer + NativeDB::kGlobalDataSize + NativeDB::kCharacterDataSize * slot);
        const auto& addonConfig = registry.Addons()[addon];

        const bool isUnlocked =
            (character->unlocked & NativeDB::kCharacterUnlockedFlag) != 0;
        if (addonConfig.initiallyUnlocked && !isUnlocked) {
            character->unlocked |= NativeDB::kCharacterUnlockedFlag;
            ++repairedUnlocks;
            Core::Logger::Warn("Restored initial addon unlock bit for slot=%d name='%s'",
                               slot,
                               addonConfig.name.c_str());
        } else if (!addonConfig.initiallyUnlocked && isUnlocked &&
                   !HasAddonProgress(*character)) {
            character->unlocked &= ~NativeDB::kCharacterUnlockedFlag;
            ++repairedUnlocks;
            Core::Logger::Warn("Cleared stale addon unlock bit for slot=%d name='%s'",
                               slot,
                               addonConfig.name.c_str());
        }

        bool repaired = false;
        if (character->strength == 0) {
            character->strength = 1;
            repaired = true;
        }
        if (character->defense == 0) {
            character->defense = 1;
            repaired = true;
        }
        if (character->magic == 0) {
            character->magic = 1;
            repaired = true;
        }
        if (character->agility == 0) {
            character->agility = 1;
            repaired = true;
        }

        if (repaired) {
            ++repairedSlots;
            Core::Logger::Warn("Repaired zero stat baseline for addon save slot=%d", slot);
        }
    }

    Core::Logger::Info("Addon save repair finished statSlots=%d unlockSlots=%d", repairedSlots, repairedUnlocks);
}

void __fastcall DetouredCreateDefaultSave(void* saveManager) {
    Core::Logger::Info("CC_CreateDefaultSave detour entered saveManager=%p", saveManager);
    g_OriginalCreateDefaultSave(saveManager);

    // CC_CreateDefaultSave receives the save manager. Its second pointer is the
    // actual save buffer, matching the per-character initializer used by v4.
    void* saveBuffer = saveManager
        ? reinterpret_cast<void**>(saveManager)[1]
        : nullptr;
    Core::Logger::Info("CC_CreateDefaultSave initializing expanded slots saveBuffer=%p", saveBuffer);
    InitializeExpandedDefaultCharacterData(saveBuffer);
    Core::Logger::Info("CC_CreateDefaultSave detour finished");
}

std::uintptr_t __fastcall DetouredSanitizeCharacterSaveSlots(void* saveManager) {
    const std::uintptr_t result = g_OriginalSanitizeCharacterSaveSlots(saveManager);
    RepairExpandedCharacterStatBaselines(saveManager);
    return result;
}

bool PatchSaveAndRosterConstants() {
    const Registry& registry = Registry::Instance();
    const int addonCount = registry.AddonCount();
    if (addonCount <= 0) {
        Core::Logger::Info("No addons registered; skipping save/roster constant patches");
        return true;
    }

    const int saveSize = registry.SaveSize();
    const int savePayloadSize = registry.SavePayloadSize();
    const int keybindsOffset = registry.KeybindsOffset();
    const int addonBytes = static_cast<int>(NativeDB::kCharacterDataSize) * addonCount;
    const int networkBase = registry.NetworkBaseCount();
    const int pastLastNetworkId = registry.PastLastNetworkId();
    const int saveSlotCount = registry.SaveCharacterSlotCount();
    const int characterTableBytes = static_cast<int>(NativeDB::kCharacterTableEntryBytes) * addonCount + 14736;
    const int firstWorkshopId = registry.FirstWorkshopId();
    const int workshopSlotTableIndex = 1131 + networkBase;
    const int workshopSlotTableByteOffset = workshopSlotTableIndex * 4;
    const int workshopSkinEntryIndex = 1130 + firstWorkshopId;
    const int workshopSkinEntryByteOffset = workshopSkinEntryIndex * 4;
    const int statsClassicCount = registry.ClassicVisibleCount();
    const int statsFreshCount = registry.FreshVisibleCount();
    const int statsTotalCount = statsFreshCount;
    const int workshopNameBase = 953 - firstWorkshopId;

    Core::Logger::Info("Patching character expansion constants: addons=%d saveSize=0x%X payload=0x%X keybinds=0x%X networkBase=%d totalNetwork=%d firstWorkshopId=%d workshopSlotTable=0x%X workshopSkinEntry=0x%X",
                       addonCount,
                       saveSize,
                       savePayloadSize,
                       keybindsOffset,
                       networkBase,
                       registry.NetworkTotalCount(),
                       firstWorkshopId,
                       workshopSlotTableByteOffset,
                       workshopSkinEntryByteOffset);

    bool ok = true;

    // Save allocation/checksum/keybind constants from NativeDB save flow.
    ok = Core::Memory::WriteU32(0xFD366, saveSize) && ok;
    ok = Core::Memory::WriteU32(0xFD390, saveSize) && ok;
    ok = Core::Memory::WriteU32(0xFD39A, savePayloadSize) && ok;
    ok = Core::Memory::WriteBytes(0x130FC0, { 0xB0, 0x01, 0xC3, 0x90, 0x90, 0x90 }) && ok;
    ok = Core::Memory::WriteBytes(0xE9143, { 0xEB, 0x2B, 0x90, 0x90, 0x90, 0x90 }) && ok;
    ok = Core::Memory::WriteU32(0x131018, saveSize) && ok;
    ok = Core::Memory::WriteU32(0x12F557, keybindsOffset) && ok;
    ok = Core::Memory::WriteU32(0x12F560, keybindsOffset) && ok;
    ok = Core::Memory::WriteU32(0x12EEED, keybindsOffset) && ok;
    ok = Core::Memory::WriteU32(0x12EEF6, keybindsOffset) && ok;
    ok = Core::Memory::WriteU32(0x13D280 + 3, keybindsOffset) && ok;
    ok = Core::Memory::WriteU32(0x13D289 + 3, keybindsOffset) && ok;
    ok = Core::Memory::WriteU32(0x13D76B + 3, keybindsOffset) && ok;
    ok = Core::Memory::WriteU32(0x13D774 + 3, keybindsOffset) && ok;
    ok = Core::Memory::WriteU32(0x13E202 + 3, keybindsOffset) && ok;
    ok = Core::Memory::WriteU32(0x13E20B + 3, keybindsOffset) && ok;
    ok = Core::Memory::WriteU32(0x12EE66, 0x832 + addonBytes) && ok;
    ok = Core::Memory::WriteU32(0x12EE6F, 0x832 + addonBytes) && ok;
    // Match v4's level-offset iterator boundary exactly: one byte past the
    // relocated keybind start.
    ok = Core::Memory::WriteU32(0x12EE34, 0x821 + addonBytes) && ok;
    ok = Core::Memory::WriteU8(0x12F26E, saveSlotCount) && ok;

    // Lobby character table and classic/fresh/workshop boundaries.
    ok = Core::Memory::WriteU8(0x83110, pastLastNetworkId) && ok;
    ok = Core::Memory::WriteU8(0x83114, pastLastNetworkId) && ok;
    ok = Core::Memory::WriteU16(0x83150, pastLastNetworkId * 4) && ok;
    ok = Core::Memory::WriteU32(0x10C895, characterTableBytes) && ok;
    ok = Core::Memory::WriteU16(0x84771, pastLastNetworkId * 4) && ok;
    ok = Core::Memory::WriteU8(0x8482D, firstWorkshopId) && ok;
    ok = Core::Memory::WriteU8(0x84842, -networkBase) && ok;
    ok = Core::Memory::WriteU8(0x84A9C, pastLastNetworkId) && ok;
    ok = Core::Memory::WriteU8(0x8EA30 + 2, pastLastNetworkId) && ok;
    ok = Core::Memory::WriteU8(0x8EA52 + 2, firstWorkshopId) && ok;
    ok = Core::Memory::WriteU8(0x87461, pastLastNetworkId) && ok;

    // DLC/fresh gates and 0x1F/0x20/0x21 style base roster comparisons.
    // Keep these identical to 5.0.3: the no-DLC Classic fallback is handled
    // only for addon IDs by the SWF dispatcher and must not alter workshops.
    ok = Core::Memory::WriteU8(0x10564A + 2, networkBase - 1) && ok;
    ok = Core::Memory::WriteU8(0x105673 + 2, -networkBase) && ok;
    ok = Core::Memory::WriteU8(0x1056A9 + 2, networkBase + 1) && ok;
    ok = Core::Memory::WriteU8(0x1056B7 + 2, networkBase + 11) && ok;
    ok = Core::Memory::WriteU8(0x1056AE + 2, networkBase + 11) && ok;
    ok = Core::Memory::WriteU8(0x1056C5 + 2, networkBase + 21) && ok;
    ok = Core::Memory::WriteU8(0x1056BC + 2, networkBase + 21) && ok;
    ok = Core::Memory::WriteU8(0x1056D3 + 2, networkBase + 31) && ok;
    ok = Core::Memory::WriteU8(0x1056CA + 2, networkBase + 31) && ok;
    ok = Core::Memory::WriteU8(0x1056DC + 2, pastLastNetworkId) && ok;

    // Selection/portrait/workshop start.
    ok = Core::Memory::WriteU8(0x14CFCE + 2, networkBase) && ok;
    ok = Core::Memory::WriteU8(0x14CFEB + 2, networkBase) && ok;
    ok = Core::Memory::WriteU8(0x8D695, networkBase + 1) && ok;
    ok = Core::Memory::WriteU8(0x8D664, pastLastNetworkId) && ok;
    ok = Core::Memory::WriteU8(0x8D66B, pastLastNetworkId) && ok;
    ok = Core::Memory::WriteU8(0x8D2AC, firstWorkshopId) && ok;
    ok = Core::Memory::WriteU8(0x8D2D6, -networkBase) && ok;
    ok = Core::Memory::WriteU8(0x8D2DC, firstWorkshopId) && ok;

    // Addon display names and workshop name fallback ranges.
    ok = Core::Memory::WriteU8(0x8A639, -firstWorkshopId) && ok;
    ok = Core::Memory::WriteU8(0x8A645, firstWorkshopId - 1) && ok;
    ok = Core::Memory::WriteU8(0x8A654, firstWorkshopId - 2) && ok;
    ok = Core::Memory::WriteU8(0x8A6E8, -firstWorkshopId) && ok;
    ok = Core::Memory::WriteU32(0x8A6FA, static_cast<std::uint32_t>(workshopNameBase)) && ok;
    ok = Core::Memory::WriteU8(0x8A72B, firstWorkshopId - 2) && ok;
    ok = Core::Memory::WriteU8(0x8FCFC, firstWorkshopId - 1) && ok;
    ok = Core::Memory::WriteU8(0x8FDA2, firstWorkshopId - 1) && ok;
    ok = Core::Memory::WriteU8(0x8A7A0, pastLastNetworkId) && ok;

    // Lobby workshop/addon behavior.
    ok = Core::Memory::WriteU8(0x8406D, firstWorkshopId) && ok;
    ok = Core::Memory::WriteU8(0x8EBA4, pastLastNetworkId) && ok;
    ok = Core::Memory::WriteU8(0x8EBE6, pastLastNetworkId) && ok;
    ok = Core::Memory::WriteU32(0x106117, static_cast<std::uint32_t>(firstWorkshopId)) && ok;
    ok = Core::Memory::WriteU8(0x105F2A, firstWorkshopId) && ok;
    ok = Core::Memory::WriteU8(0x8FC81, pastLastNetworkId) && ok;
    ok = Core::Memory::WriteU8(0x8FC95, pastLastNetworkId) && ok;
    ok = Core::Memory::WriteU8(0x105DF6, firstWorkshopId) && ok;
    ok = Core::Memory::WriteU8(0x1608AA, -firstWorkshopId) && ok;

    // Stats menu character iterator. Classic addons use classic frames; fresh addons move
    // the workshop block after the expanded fresh roster.
    ok = Core::Memory::WriteU32(0x14CF64, static_cast<std::uint32_t>(statsTotalCount)) && ok;
    ok = Core::Memory::WriteU8(0x14CFBF, statsTotalCount) && ok;
    ok = Core::Memory::WriteU8(0x14CF76, statsClassicCount) && ok;
    ok = Core::Memory::WriteU8(0x14CFA4, statsClassicCount) && ok;
    ok = Core::Memory::WriteU8(0x14CFD0, statsFreshCount) && ok;
    ok = Core::Memory::WriteU8(0x14CFED, statsFreshCount) && ok;

    // Ready-state workshop validation.
    ok = Core::Memory::WriteU8(0x87C5C, firstWorkshopId) && ok;
    ok = Core::Memory::WriteU32(0x87D7E, static_cast<std::uint32_t>(workshopSkinEntryByteOffset)) && ok;
    ok = Core::Memory::WriteU8(0x87DDC, firstWorkshopId) && ok;
    ok = Core::Memory::WriteU32(0x87E96, static_cast<std::uint32_t>(workshopSlotTableByteOffset)) && ok;
    ok = Core::Memory::WriteU32(0x87E9B, static_cast<std::uint32_t>(firstWorkshopId)) && ok;
    ok = Core::Memory::WriteU8(0x87FC8, firstWorkshopId) && ok;
    ok = Core::Memory::WriteU8(0x88047, firstWorkshopId) && ok;
    ok = Core::Memory::WriteU8(0x88090, pastLastNetworkId) && ok;

    Core::Logger::Info("PatchSaveAndRosterConstants result=%d", ok ? 1 : 0);
    return ok;
}

bool InstallDefaultSaveHook() {
    g_OriginalCreateDefaultSave =
        reinterpret_cast<CreateDefaultSaveFn>(NativeDB::NativeAddress(NativeDB::Natives::CC_CreateDefaultSave));

    Core::Logger::Info("Installing CC_CreateDefaultSave detour at %p", reinterpret_cast<void*>(g_OriginalCreateDefaultSave));
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    const LONG attach = DetourAttach(reinterpret_cast<PVOID*>(&g_OriginalCreateDefaultSave), DetouredCreateDefaultSave);
    if (attach != NO_ERROR) {
        Core::Logger::Error("DetourAttach CC_CreateDefaultSave failed: %ld", attach);
        DetourTransactionAbort();
        return false;
    }
    const LONG commit = DetourTransactionCommit();
    Core::Logger::Info("CC_CreateDefaultSave detour commit result=%ld", commit);
    return commit == NO_ERROR;
}

bool InstallSanitizeSaveHook() {
    g_OriginalSanitizeCharacterSaveSlots =
        reinterpret_cast<SanitizeCharacterSaveSlotsFn>(
            NativeDB::NativeAddress(NativeDB::Natives::CC_SanitizeCharacterSaveSlots));

    Core::Logger::Info("Installing CC_SanitizeCharacterSaveSlots detour at %p",
                       reinterpret_cast<void*>(g_OriginalSanitizeCharacterSaveSlots));
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    const LONG attach = DetourAttach(
        reinterpret_cast<PVOID*>(&g_OriginalSanitizeCharacterSaveSlots),
        DetouredSanitizeCharacterSaveSlots);
    if (attach != NO_ERROR) {
        Core::Logger::Error("DetourAttach CC_SanitizeCharacterSaveSlots failed: %ld", attach);
        DetourTransactionAbort();
        return false;
    }
    const LONG commit = DetourTransactionCommit();
    Core::Logger::Info("CC_SanitizeCharacterSaveSlots detour commit result=%ld", commit);
    return commit == NO_ERROR;
}

} // namespace

bool CharacterExpansion::Install() {
    Core::Logger::Info("CharacterExpansion::Install begin addonCount=%d", Registry::Instance().AddonCount());
    bool ok = PatchSaveAndRosterConstants();
    Core::Logger::Info("Installing LobbyPortraitExpansion");
    ok = Lobby::LobbyPortraitExpansion::Install() && ok;
    Core::Logger::Info("Installing Cok6LoadHook");
    ok = SWF::Cok6LoadHook::Install() && ok;
    Core::Logger::Info("Installing CharacterSyncExpansion");
    ok = Network::CharacterSyncExpansion::Install() && ok;
    Core::Logger::Info("Installing default save hook");
    ok = InstallDefaultSaveHook() && ok;
    Core::Logger::Info("Installing save sanitization hook");
    ok = InstallSanitizeSaveHook() && ok;
    Core::Logger::Info("CharacterExpansion::Install result=%d", ok ? 1 : 0);
    return ok;
}

void CharacterExpansion::Remove() {
    Core::Logger::Info("CharacterExpansion::Remove begin");
    Network::CharacterSyncExpansion::Remove();
    SWF::Cok6LoadHook::Remove();
    if (g_OriginalSanitizeCharacterSaveSlots) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(
            reinterpret_cast<PVOID*>(&g_OriginalSanitizeCharacterSaveSlots),
            DetouredSanitizeCharacterSaveSlots);
        DetourTransactionCommit();
        g_OriginalSanitizeCharacterSaveSlots = nullptr;
    }
    if (!g_OriginalCreateDefaultSave) {
        Core::Logger::Info("CharacterExpansion::Remove no default save hook installed");
        return;
    }
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID*>(&g_OriginalCreateDefaultSave), DetouredCreateDefaultSave);
    DetourTransactionCommit();
    g_OriginalCreateDefaultSave = nullptr;
    Core::Logger::Info("CharacterExpansion::Remove complete");
}

} // namespace HookCrashers::Characters
