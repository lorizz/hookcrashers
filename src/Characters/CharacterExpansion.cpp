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

using CreateDefaultSaveFn = void(__fastcall*)(void* saveBuffer);
CreateDefaultSaveFn g_OriginalCreateDefaultSave = nullptr;

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

void __fastcall DetouredCreateDefaultSave(void* saveBuffer) {
    Core::Logger::Info("CC_CreateDefaultSave detour entered saveBuffer=%p", saveBuffer);
    g_OriginalCreateDefaultSave(saveBuffer);
    InitializeExpandedDefaultCharacterData(saveBuffer);
    Core::Logger::Info("CC_CreateDefaultSave detour finished");
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
    const int characterTableBytes = static_cast<int>(NativeDB::kCharacterTableEntryBytes) * addonCount + 14728;
    const int firstWorkshopId = registry.FirstWorkshopId();

    Core::Logger::Info("Patching character expansion constants: addons=%d saveSize=0x%X payload=0x%X keybinds=0x%X networkBase=%d totalNetwork=%d firstWorkshopId=%d",
                       addonCount,
                       saveSize,
                       savePayloadSize,
                       keybindsOffset,
                       networkBase,
                       registry.NetworkTotalCount(),
                       firstWorkshopId);

    bool ok = true;

    // Save allocation/checksum/keybind constants from NativeDB save flow.
    ok = Core::Memory::WriteU32(0xFD2E6, saveSize) && ok;
    ok = Core::Memory::WriteU32(0xFD310, saveSize) && ok;
    ok = Core::Memory::WriteU32(0xFD31A, savePayloadSize) && ok;
    ok = Core::Memory::WriteU32(0x130F68, saveSize) && ok;
    ok = Core::Memory::WriteU32(0x12F4A7, keybindsOffset) && ok;
    ok = Core::Memory::WriteU32(0x12F4B0, keybindsOffset) && ok;
    ok = Core::Memory::WriteU32(0x12EE3D, keybindsOffset) && ok;
    ok = Core::Memory::WriteU32(0x12EE46, keybindsOffset) && ok;
    ok = Core::Memory::WriteU32(0x12EDB6, 0x832 + addonBytes) && ok;
    ok = Core::Memory::WriteU32(0x12EDBF, 0x832 + addonBytes) && ok;
    ok = Core::Memory::WriteU32(0x12ED84, 0x821 + addonBytes) && ok;
    ok = Core::Memory::WriteU8(0x12F1BE, saveSlotCount) && ok;

    // Lobby character table and classic/fresh/workshop boundaries.
    ok = Core::Memory::WriteU8(0x830A0, pastLastNetworkId) && ok;
    ok = Core::Memory::WriteU8(0x830A4, pastLastNetworkId) && ok;
    ok = Core::Memory::WriteU16(0x830E0, pastLastNetworkId * 4) && ok;
    ok = Core::Memory::WriteU32(0x10C805, characterTableBytes) && ok;
    ok = Core::Memory::WriteU16(0x84701, pastLastNetworkId * 4) && ok;
    ok = Core::Memory::WriteU8(0x847BD, firstWorkshopId) && ok;
    ok = Core::Memory::WriteU8(0x847D2, -networkBase) && ok;
    ok = Core::Memory::WriteU8(0x84A2C, pastLastNetworkId) && ok;
    ok = Core::Memory::WriteU8(0x8E9A0 + 2, pastLastNetworkId) && ok;
    ok = Core::Memory::WriteU8(0x8E9C2 + 2, firstWorkshopId) && ok;
    ok = Core::Memory::WriteU8(0x873F1, pastLastNetworkId) && ok;

    // DLC/fresh gates and 0x1F/0x20/0x21 style base roster comparisons.
    ok = Core::Memory::WriteU8(0x1055CA + 2, networkBase - 1) && ok;
    ok = Core::Memory::WriteU8(0x1055F3 + 2, -networkBase) && ok;
    ok = Core::Memory::WriteU8(0x105629 + 2, networkBase + 1) && ok;
    ok = Core::Memory::WriteU8(0x105637 + 2, networkBase + 11) && ok;
    ok = Core::Memory::WriteU8(0x10562E + 2, networkBase + 11) && ok;
    ok = Core::Memory::WriteU8(0x105645 + 2, networkBase + 21) && ok;
    ok = Core::Memory::WriteU8(0x10563C + 2, networkBase + 21) && ok;
    ok = Core::Memory::WriteU8(0x105653 + 2, networkBase + 31) && ok;
    ok = Core::Memory::WriteU8(0x10564A + 2, networkBase + 31) && ok;
    ok = Core::Memory::WriteU8(0x10565C + 2, pastLastNetworkId) && ok;

    // Selection/portrait/workshop start.
    ok = Core::Memory::WriteU8(0x14CF2E + 2, networkBase) && ok;
    ok = Core::Memory::WriteU8(0x14CF4B + 2, networkBase) && ok;
    ok = Core::Memory::WriteU8(0x8D625, networkBase + 1) && ok;
    ok = Core::Memory::WriteU8(0x8D5F4, pastLastNetworkId) && ok;
    ok = Core::Memory::WriteU8(0x8D5FB, pastLastNetworkId) && ok;
    ok = Core::Memory::WriteU8(0x8D23C, firstWorkshopId) && ok;
    ok = Core::Memory::WriteU8(0x8D26C, firstWorkshopId) && ok;

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
    Core::Logger::Info("CharacterExpansion::Install result=%d", ok ? 1 : 0);
    return ok;
}

void CharacterExpansion::Remove() {
    Core::Logger::Info("CharacterExpansion::Remove begin");
    Network::CharacterSyncExpansion::Remove();
    SWF::Cok6LoadHook::Remove();
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
