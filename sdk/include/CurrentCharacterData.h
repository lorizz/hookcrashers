#pragma once
#include <cstdint>
#ifndef HOOKCRASHERS_API
#if defined(HOOKCRASHERS_EXPORTS)
#define HOOKCRASHERS_API __declspec(dllexport)
#else
#define HOOKCRASHERS_API __declspec(dllimport)
#endif
#endif

namespace HookCrashers {
// Called on the game thread after snapshot publication. Must return promptly.
using UpdateCallback = void(__stdcall*)();
enum class CharacterDataResult : std::uint32_t {
    Ok, InvalidPlayer, InvalidBuffer, Unavailable, Stale, SequenceChanged, InvalidProperty
};
enum class CharacterDataSource : std::uint32_t { None, LivePlayer, Hud };
enum class CharacterDataState : std::uint32_t { Unavailable, Inactive, HudOnly, InLevel, Loading };
enum class CharacterPropertyKind : std::uint32_t {
    Undefined, Boolean, Integer, Float, String, Object, Array, Opaque
};
enum CharacterDataFlags : std::uint32_t {
    CharacterData_Truncated = 1, CharacterData_ReadFault = 2,
    CharacterData_FrameBoundary = 4
};
enum CharacterPropertyFlags : std::uint32_t {
    CharacterProperty_Truncated = 1, CharacterProperty_Reference = 2,
    CharacterProperty_ReadFault = 4, CharacterProperty_DepthLimit = 8
};
// Numeric validity bits correspond to CurrentCharacterField values.
enum class CurrentCharacterField : std::uint32_t {
    CharacterId, SavePort, Alive, Health, HealthMax, MagicCurrent, MagicMax,
    Level, Experience, ExperienceNext, Gold, Strength, Defense, Magic, Agility,
    Weapon, WeaponRepaint, AnimalType, HealthPotions, Bombs, Beefies, EquippedItem,
    Kills, Resists, UnspentStatPoints, InsaneMode, Count
};

#pragma pack(push, 8)
struct CurrentCharacterData {
    std::uint32_t structSize;
    std::uint32_t schemaVersion;
    std::uint64_t sequence;
    std::uint64_t capturedAtMilliseconds; // GetTickCount64 time domain
    CharacterDataResult result;
    CharacterDataSource source;
    CharacterDataState state;
    std::uint32_t playerSlot; // 1..4: HUD slot; distinct from port/character ID
    std::uint32_t propertyCount;
    std::uint32_t flags;
    std::uint64_t validFields;
    double characterId, savePort, alive, health, healthMax, magicCurrent, magicMax;
    double level, experience, experienceNext, gold, strength, defense, magic, agility;
    double weapon, weaponRepaint, animalType, healthPotions, bombs, beefies, equippedItem;
    double kills, resists, unspentStatPoints, insaneMode;
};
struct CurrentCharacterProperty {
    std::uint32_t structSize;
    std::uint32_t schemaVersion;
    std::uint64_t sequence;
    std::uint32_t parentIndex; // UINT32_MAX for a direct player/HUD property
    std::uint32_t referenceId; // Snapshot-local object ID, never a VM pointer
    CharacterPropertyKind kind;
    std::uint32_t nativeType;
    std::uint32_t flags;
    std::uint32_t reserved;
    double number; // Boolean/int/float; array length for Array
    char name[64];
    char text[128]; // UTF-8 bytes from the VM string table; truncation is flagged
};
#pragma pack(pop)
static_assert(sizeof(CurrentCharacterData) == 264, "CurrentCharacterData ABI size");
static_assert(sizeof(CurrentCharacterProperty) == 240, "CurrentCharacterProperty ABI size");
inline bool HasCharacterField(const CurrentCharacterData& data, CurrentCharacterField field) {
    return (data.validFields & (std::uint64_t{1} << static_cast<unsigned>(field))) != 0;
}
}

extern "C" {
HOOKCRASHERS_API bool __stdcall HookCrashers_RegisterUpdateCallback(HookCrashers::UpdateCallback callback);
HOOKCRASHERS_API bool __stdcall HookCrashers_UnregisterUpdateCallback(HookCrashers::UpdateCallback callback);
HOOKCRASHERS_API HookCrashers::CharacterDataResult __stdcall HookCrashers_GetCurrentCharacterData(
    std::uint32_t playerSlot, HookCrashers::CurrentCharacterData* output, std::uint32_t outputSize);
HOOKCRASHERS_API HookCrashers::CharacterDataResult __stdcall HookCrashers_GetCurrentCharacterProperty(
    std::uint32_t playerSlot, std::uint64_t expectedSequence, std::uint32_t propertyIndex,
    HookCrashers::CurrentCharacterProperty* output, std::uint32_t outputSize);
}

namespace HookCrashers {
inline bool RegisterUpdateCallback(UpdateCallback callback) {
    return HookCrashers_RegisterUpdateCallback(callback);
}
inline bool UnregisterUpdateCallback(UpdateCallback callback) {
    return HookCrashers_UnregisterUpdateCallback(callback);
}
inline CurrentCharacterData GetCurrentCharacterData(std::uint32_t playerSlot = 1) {
    CurrentCharacterData data {};
    data.result = HookCrashers_GetCurrentCharacterData(playerSlot, &data, sizeof(data));
    return data;
}
}
