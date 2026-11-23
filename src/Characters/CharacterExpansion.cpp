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
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace HookCrashers::Characters {
namespace {

using CreateDefaultSaveFn = void(__fastcall*)(void* saveManager);
CreateDefaultSaveFn g_OriginalCreateDefaultSave = nullptr;

using SanitizeCharacterSaveSlotsFn = std::uintptr_t(__fastcall*)(void* saveManager);
SanitizeCharacterSaveSlotsFn g_OriginalSanitizeCharacterSaveSlots = nullptr;
bool g_DefaultSaveHookInstalled = false;
bool g_SanitizeSaveHookInstalled = false;

// Every constant patch carries the byte(s) the supported build has at that
// address. All of them are verified before anything is written, so a different
// build (or another mod patching the same code) leaves the game untouched.
struct ConstantPatch {
    std::uintptr_t rva;
    std::uint8_t width;      // 1, 2 or 4 bytes
    std::uint32_t original;  // value in castle.exe 0x6A4D42BF
    std::uint32_t value;
};

struct BytePatch {
    std::uintptr_t rva;
    std::vector<std::uint8_t> original;
    std::vector<std::uint8_t> replacement;
};

struct PatchSet {
    std::vector<ConstantPatch> constants;
    std::vector<BytePatch> bytes;
};

// Values written by an earlier install (Install can run twice: once from a Lua
// InstallCharacterExpansion call and once automatically after all mods load).
std::unordered_map<std::uintptr_t, std::uint32_t> g_WrittenConstants;

std::uint32_t Mask(std::uint8_t width) {
    return width >= 4 ? 0xFFFFFFFFu : ((1u << (width * 8)) - 1u);
}

std::uint32_t ReadConstant(std::uintptr_t rva, std::uint8_t width) {
    std::uint32_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(Core::Memory::Va(rva)), width);
    return value & Mask(width);
}

bool ApplyVerified(const PatchSet& set) {
    bool matches = true;
    for (const ConstantPatch& patch : set.constants) {
        const std::uint32_t current = ReadConstant(patch.rva, patch.width);
        const auto previous = g_WrittenConstants.find(patch.rva);
        const bool known = current == (patch.original & Mask(patch.width)) ||
                           (previous != g_WrittenConstants.end() && current == previous->second);
        if (!known) {
            Core::Logger::Error("Character expansion: unexpected value at rva=0x%08X width=%u current=0x%X expected=0x%X",
                                static_cast<unsigned>(patch.rva), patch.width, current, patch.original);
            matches = false;
        }
    }
    for (const BytePatch& patch : set.bytes) {
        const auto* current = reinterpret_cast<const std::uint8_t*>(Core::Memory::Va(patch.rva));
        if (std::memcmp(current, patch.original.data(), patch.original.size()) != 0 &&
            std::memcmp(current, patch.replacement.data(), patch.replacement.size()) != 0) {
            Core::Logger::Error("Character expansion: unexpected code at rva=0x%08X", static_cast<unsigned>(patch.rva));
            matches = false;
        }
    }
    if (!matches) {
        Core::Logger::Error("Character expansion: game code does not match the supported build; no constants patched");
        return false;
    }

    bool ok = true;
    for (const ConstantPatch& patch : set.constants) {
        const std::uint32_t value = patch.value & Mask(patch.width);
        switch (patch.width) {
        case 1: ok = Core::Memory::WriteU8(patch.rva, static_cast<int>(value)) && ok; break;
        case 2: ok = Core::Memory::WriteU16(patch.rva, static_cast<int>(value)) && ok; break;
        default: ok = Core::Memory::WriteU32(patch.rva, value) && ok; break;
        }
        g_WrittenConstants[patch.rva] = value;
    }
    for (const BytePatch& patch : set.bytes) {
        ok = Core::Memory::WriteBytes(patch.rva, patch.replacement) && ok;
    }
    return ok;
}

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

    PatchSet set;
    auto patch = [&set](std::uintptr_t rva, std::uint8_t width, std::uint32_t original, int value) {
        set.constants.push_back({rva, width, original, static_cast<std::uint32_t>(value)});
    };
    auto bytes = [&set](std::uintptr_t rva, std::vector<std::uint8_t> original, std::vector<std::uint8_t> replacement) {
        set.bytes.push_back({rva, std::move(original), std::move(replacement)});
    };


    // Save allocation/checksum/keybind constants from NativeDB save flow.
    patch(0xfd366, 4, 0x850, saveSize);
    patch(0xfd390, 4, 0x850, saveSize);
    patch(0xfd39a, 4, 0x84c, savePayloadSize);
    // Save-size validators accept any size (vanilla accepts 0x850/0x640/0x610 only).
    bytes(0x130FC0, {0x81, 0xF9, 0x50, 0x08, 0x00, 0x00}, {0xB0, 0x01, 0xC3, 0x90, 0x90, 0x90}); // cmp ecx,0x850 -> mov al,1; ret
    bytes(0xE9143, {0x81, 0xFE, 0x50, 0x08, 0x00, 0x00}, {0xEB, 0x2B, 0x90, 0x90, 0x90, 0x90}); // CC_ReadSaveFile size check -> jmp valid path
    patch(0x131018, 4, 0x850, saveSize);
    patch(0x12f557, 4, 0x820, keybindsOffset);
    patch(0x12f560, 4, 0x820, keybindsOffset);
    patch(0x12eeed, 4, 0x820, keybindsOffset);
    patch(0x12eef6, 4, 0x820, keybindsOffset);
    patch(0x13d283, 4, 0x820, keybindsOffset);
    patch(0x13d28c, 4, 0x820, keybindsOffset);
    patch(0x13d76e, 4, 0x820, keybindsOffset);
    patch(0x13d777, 4, 0x820, keybindsOffset);
    patch(0x13e205, 4, 0x820, keybindsOffset);
    patch(0x13e20e, 4, 0x820, keybindsOffset);
    patch(0x12ee66, 4, 0x832, 0x832 + addonBytes);
    patch(0x12ee6f, 4, 0x832, 0x832 + addonBytes);
    // Match v4's level-offset iterator boundary exactly: one byte past the
    // relocated keybind start.
    patch(0x12ee34, 4, 0x821, 0x821 + addonBytes);
    patch(0x12f26e, 1, 0x2a, saveSlotCount);

    // Lobby character table and classic/fresh/workshop boundaries.
    patch(0x83110, 1, 0x49, pastLastNetworkId);
    patch(0x83114, 1, 0x49, pastLastNetworkId);
    patch(0x83150, 2, 0x124, pastLastNetworkId * 4);
    patch(0x10c895, 4, 0x3990, characterTableBytes);
    patch(0x84771, 2, 0x124, pastLastNetworkId * 4);
    patch(0x8482d, 1, 0x21, firstWorkshopId);
    patch(0x84842, 1, 0xe0, -networkBase);
    patch(0x84a9c, 1, 0x49, pastLastNetworkId);
    patch(0x8ea32, 1, 0x49, pastLastNetworkId);
    patch(0x8ea54, 1, 0x21, firstWorkshopId);
    patch(0x87461, 1, 0x49, pastLastNetworkId);

    // DLC/fresh gates and 0x1F/0x20/0x21 style base roster comparisons.
    // Keep these identical to 5.0.3: the no-DLC Classic fallback is handled
    // only for addon IDs by the SWF dispatcher and must not alter workshops.
    patch(0x10564c, 1, 0x1f, networkBase - 1);
    patch(0x105675, 1, 0xe0, -networkBase);
    patch(0x1056ab, 1, 0x21, networkBase + 1);
    patch(0x1056b9, 1, 0x2b, networkBase + 11);
    patch(0x1056b0, 1, 0x2b, networkBase + 11);
    patch(0x1056c7, 1, 0x35, networkBase + 21);
    patch(0x1056be, 1, 0x35, networkBase + 21);
    patch(0x1056d5, 1, 0x3f, networkBase + 31);
    patch(0x1056cc, 1, 0x3f, networkBase + 31);
    patch(0x1056de, 1, 0x49, pastLastNetworkId);

    // Selection/portrait/workshop start.
    // 0x14CFD0/0x14CFED belong to the stats iterator below (CC_StatsMenu at 0x14CF50);
    // they used to be written here with networkBase and then overwritten there.
    patch(0x8d695, 1, 0x21, networkBase + 1);
    patch(0x8d664, 1, 0x49, pastLastNetworkId);
    patch(0x8d66b, 1, 0x49, pastLastNetworkId);
    patch(0x8d2ac, 1, 0x21, firstWorkshopId);
    // `lea ecx, [edi-0x20]` at 0x8D2D3: the imm8 is at 0x8D2D5. Until 5.2.0 this wrote
    // 0x8D2D6, the opcode of the following `cmp ecx, eax`, turning it into `loopne`.
    patch(0x8d2d5, 1, 0xe0, -networkBase);
    patch(0x8d2dc, 1, 0x21, firstWorkshopId);

    // Addon display names and workshop name fallback ranges.
    patch(0x8a639, 1, 0xdf, -firstWorkshopId);
    patch(0x8a645, 1, 0x20, firstWorkshopId - 1);
    patch(0x8a654, 1, 0x1f, firstWorkshopId - 2);
    patch(0x8a6e8, 1, 0xdf, -firstWorkshopId);
    patch(0x8a6fa, 4, 0x398, static_cast<std::uint32_t>(workshopNameBase));
    patch(0x8a72b, 1, 0x1f, firstWorkshopId - 2);
    patch(0x8fcfc, 1, 0x21, firstWorkshopId - 1);
    patch(0x8fda2, 1, 0x21, firstWorkshopId - 1);
    patch(0x8a7a0, 1, 0x49, pastLastNetworkId);

    // Lobby workshop/addon behavior.
    patch(0x8406d, 1, 0x21, firstWorkshopId);
    patch(0x8eba4, 1, 0x49, pastLastNetworkId);
    patch(0x8ebe6, 1, 0x49, pastLastNetworkId);
    patch(0x106117, 4, 0x21, static_cast<std::uint32_t>(firstWorkshopId));
    patch(0x105f2a, 1, 0x21, firstWorkshopId);
    patch(0x8fc81, 1, 0x49, pastLastNetworkId);
    patch(0x8fc95, 1, 0x49, pastLastNetworkId);
    patch(0x105df6, 1, 0x21, firstWorkshopId);
    patch(0x1608aa, 1, 0xdf, -firstWorkshopId);

    // Stats menu character iterator. Classic addons use classic frames; fresh addons move
    // the workshop block after the expanded fresh roster.
    patch(0x14cf64, 4, 0x2a, static_cast<std::uint32_t>(statsTotalCount));
    patch(0x14cfbf, 1, 0x2a, statsTotalCount);
    patch(0x14cf76, 1, 0x1f, statsClassicCount);
    patch(0x14cfa4, 1, 0x1f, statsClassicCount);
    patch(0x14cfd0, 1, 0x20, statsFreshCount);
    patch(0x14cfed, 1, 0x20, statsFreshCount);

    // Ready-state workshop validation.
    patch(0x87c5c, 1, 0x21, firstWorkshopId);
    patch(0x87d7e, 4, 0x122c, static_cast<std::uint32_t>(workshopSkinEntryByteOffset));
    patch(0x87ddc, 1, 0x21, firstWorkshopId);
    patch(0x87e96, 4, 0x122c, static_cast<std::uint32_t>(workshopSlotTableByteOffset));
    patch(0x87e9b, 4, 0x21, static_cast<std::uint32_t>(firstWorkshopId));
    patch(0x87fc8, 1, 0x21, firstWorkshopId);
    patch(0x88047, 1, 0x21, firstWorkshopId);
    patch(0x88090, 1, 0x49, pastLastNetworkId);


    const bool ok = ApplyVerified(set);
    Core::Logger::Info("PatchSaveAndRosterConstants result=%d", ok ? 1 : 0);
    return ok;
}

bool InstallDefaultSaveHook() {
    // A second Install must not detour the trampoline again (the hook would run twice).
    if (g_DefaultSaveHookInstalled) return true;
    g_OriginalCreateDefaultSave =
        reinterpret_cast<CreateDefaultSaveFn>(NativeDB::NativeAddress(NativeDB::Natives::CC_CreateDefaultSave));

    Core::Logger::Info("Installing CC_CreateDefaultSave detour at %p", reinterpret_cast<void*>(g_OriginalCreateDefaultSave));
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    const LONG attach = DetourAttach(reinterpret_cast<PVOID*>(&g_OriginalCreateDefaultSave), DetouredCreateDefaultSave);
    if (attach != NO_ERROR) {
        Core::Logger::Error("DetourAttach CC_CreateDefaultSave failed: %ld", attach);
        DetourTransactionAbort();
        g_OriginalCreateDefaultSave = nullptr;
        return false;
    }
    const LONG commit = DetourTransactionCommit();
    Core::Logger::Info("CC_CreateDefaultSave detour commit result=%ld", commit);
    g_DefaultSaveHookInstalled = commit == NO_ERROR;
    if (!g_DefaultSaveHookInstalled) g_OriginalCreateDefaultSave = nullptr;
    return g_DefaultSaveHookInstalled;
}

bool InstallSanitizeSaveHook() {
    if (g_SanitizeSaveHookInstalled) return true;
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
        g_OriginalSanitizeCharacterSaveSlots = nullptr;
        return false;
    }
    const LONG commit = DetourTransactionCommit();
    Core::Logger::Info("CC_SanitizeCharacterSaveSlots detour commit result=%ld", commit);
    g_SanitizeSaveHookInstalled = commit == NO_ERROR;
    if (!g_SanitizeSaveHookInstalled) g_OriginalSanitizeCharacterSaveSlots = nullptr;
    return g_SanitizeSaveHookInstalled;
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
    if (g_SanitizeSaveHookInstalled) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(
            reinterpret_cast<PVOID*>(&g_OriginalSanitizeCharacterSaveSlots),
            DetouredSanitizeCharacterSaveSlots);
        DetourTransactionCommit();
        g_OriginalSanitizeCharacterSaveSlots = nullptr;
        g_SanitizeSaveHookInstalled = false;
    }
    if (!g_DefaultSaveHookInstalled) {
        Core::Logger::Info("CharacterExpansion::Remove no default save hook installed");
        return;
    }
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID*>(&g_OriginalCreateDefaultSave), DetouredCreateDefaultSave);
    DetourTransactionCommit();
    g_OriginalCreateDefaultSave = nullptr;
    g_DefaultSaveHookInstalled = false;
    Core::Logger::Info("CharacterExpansion::Remove complete");
}

} // namespace HookCrashers::Characters
