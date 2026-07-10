#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace HookCrashers::NativeDB {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr u32 kWindowsImageBase = 0x00100000;
constexpr u8 kCharacterUnlockedFlag = 0x80;
constexpr std::size_t kGlobalDataSize = 0x40;
constexpr std::size_t kCharacterDataSize = 0x30;
constexpr std::size_t kSaveFileSize = 0x850;
constexpr std::size_t kSavePayloadSize = 0x84C;
constexpr std::size_t kKeybindsOffset = 0x820;
constexpr std::size_t kKeybindsSize = 0x2C;
constexpr std::size_t kChecksumOffset = 0x84C;
constexpr std::size_t kVanillaBaseCharacterCount = 32;
constexpr std::size_t kClassicVisibleCharacterCount = 31;
constexpr std::size_t kWorkshopSaveSlotCount = 10;
constexpr std::size_t kSaveCharacterSlotCount = 42;
constexpr std::size_t kWorkshopNetworkSlotCount = 40;
constexpr std::size_t kCharacterTableEntryBytes = 0x20;

#pragma pack(push, 1)

struct CC_GlobalData {
    u8 vibration;                         // +0x00, default 1
    u8 voice_volume;                      // +0x01, default 100
    u8 music_volume;                      // +0x02, default 75
    u8 sfx_volume;                        // +0x03, default 80
    u8 gore;                              // +0x04, default 1
    u8 unknown_05;                        // +0x05, padding or small profile flag
    u8 profile_flags;                     // +0x06, fresh/new graphics mirror; still gated by DLC bit 3
    u8 arenas_bitflag;                    // +0x07
    std::array<u8, 4> animals_bitflags;   // +0x08
    std::array<u8, 16> items_bitflags;    // +0x0C
    std::array<u8, 8> items_expansion_bitflags; // +0x1C, expansion/DLC item flags
    std::array<u8, 12> reserved_24;       // +0x24
    u32 back_off_barbarian_time;          // +0x30
    u8 back_off_barbarian_character;      // +0x34
    std::array<u8, 11> reserved_35;       // +0x35
};

struct CC_CharacterData {
    u8 unlocked;                          // +0x00, bit 7 / 0x80 means unlocked
    u8 level;                             // +0x01, SWF getter returns level + 1
    std::array<u8, 4> experience;         // +0x02, little-endian
    u8 weapon;                            // +0x06
    u8 animal;                            // +0x07
    u8 strength;                          // +0x08, clamped 1..0x19
    u8 defense;                           // +0x09, clamped 1..0x19
    u8 magic;                             // +0x0A, clamped 1..0x19
    u8 agility;                           // +0x0B, clamped 1..0x19
    std::array<u8, 3> normal_levels_bitflags; // +0x0C
    u8 potions;                           // +0x0F
    u8 bombs;                             // +0x10
    u8 beefies;                           // +0x11
    u8 relics_bitflag;                    // +0x12
    std::array<u8, 4> gold;               // +0x13, little-endian
    u8 insane_mode;                       // +0x17, separate from unlocked at +0x00
    std::array<u8, 3> insane_levels_bitflags; // +0x18
    u8 skull;                             // +0x1B, completion/skull state exposed to SWF
    u8 shovel_items;                      // +0x1C
    u8 princess_kisses;                   // +0x1D
    std::array<u16, 4> arena_wins;        // +0x1E
    u32 arena_losses;                     // +0x26
    u32 arena_points;                     // +0x2A
    std::array<u8, 2> reserved_2e;        // +0x2E
};

struct CC_SaveFile {
    CC_GlobalData global;                                 // +0x000
    std::array<CC_CharacterData, 42> characters;          // +0x040, 32 base + 10 workshop/addon save slots
    std::array<u8, kKeybindsSize> keybinds;               // +0x820
    u32 checksum;                                         // +0x84C
};

struct CC_SWFReader {
    std::array<u8, 0x0F0> unknown_000;
    u8* tagData;                          // +0x0F0
    std::array<u8, 0x14> unknown_0f4;
    void* tagIndexOrFrameMap;             // +0x108
    std::array<u8, 0x1C> unknown_10c;
    u32 tagCursor;                        // +0x128
    u32 currentTagLength;                 // +0x12C
    std::array<u8, 0x08> unknown_130;
    u16 spriteOrFrameId;                  // +0x138
    std::array<u8, 0x16> unknown_13a;
    void* movieContext;                   // +0x150
};

struct CC_SWFTagHandlerTable {
    void** handlers;                      // NativeDB: base + 0x2A2EE0, entries are handler pointers
};

struct CC_CharacterNetId {
    u32 lo;
    u32 hi;
};

struct CC_OwnedBuffer20 {
    void* heapData;                       // +0x00
    u32 field_04;                         // +0x04
    u32 length;                           // +0x08
    u32 capacity;                         // +0x0C
    u32 inlineThreshold;                  // +0x10
};

struct CC_LobbyWorkshopSaveSlot32 {
    std::array<u8, 0x14> unknown_00;      // +0x00
    int workshopCharacterIdOrSentinel;    // +0x14, -1 means empty in selection logic
    u64 presenceValue;                    // +0x18, non-zero marks populated
};

struct CC_CharacterRuntimeNode {
    CC_OwnedBuffer20 buffer0;             // +0x00
    std::array<u8, 0x04> unknown_14;      // +0x14
    u32 field_18;                         // +0x18
    u32 field_1c;                         // +0x1C
    CC_OwnedBuffer20 buffer1;             // +0x20
    u8 flag_34;                           // +0x34
    u8 flag_35;                           // +0x35
    std::array<u8, 0x02> pad_36;          // +0x36
    std::array<u8, 0x74> raw_38;          // +0x38
    u32 field_ac;                         // +0xAC
    int sentinel_b0;                      // +0xB0, initialized to -1
    std::array<u8, 0x04> unknown_b4;      // +0xB4
    u32 field_b8;                         // +0xB8
    u32 field_bc;                         // +0xBC
    u32 field_c0;                         // +0xC0
    u8 flag_c4;                           // +0xC4
    u8 flag_c5;                           // +0xC5
    std::array<u8, 0x02> pad_c6;          // +0xC6
};

struct CC_CharacterTableEntry {
    u8 active;                            // +0x00
    std::array<u8, 0x07> pad_01;          // +0x01
    CC_OwnedBuffer20 assetPath;           // +0x08
    int workshopCharacterIdOrSentinel;    // +0x1C
    u32 workshopIdentityLo;               // +0x20
    u32 workshopIdentityHi;               // +0x24
    CC_CharacterRuntimeNode node;         // +0x28
    std::array<u8, 0xFA0> raw_f0;         // +0xF0
};

struct CC_LobbyManagerPartial {
    void* field_0000;                     // +0x0000
    void* field_0004;                     // +0x0004
    void* field_0008;                     // +0x0008
    void* field_000c;                     // +0x000C
    void* field_0010;                     // +0x0010
    void* field_0014;                     // +0x0014
    std::array<u8, 0x20> pad_0018;        // +0x0018
    CC_CharacterTableEntry* characterArrayBegin;       // +0x0038
    CC_CharacterTableEntry* characterArrayEnd;         // +0x003C
    CC_CharacterTableEntry* characterArrayCapacityEnd; // +0x0040
};

struct CC_ProfileManagerPartial {
    std::array<u8, 0x118> unknown_000;
    void* dlcEntitlementTable;            // +0x118, entry stride 0x0C
    std::array<u8, 0x04> unknown_11c;
    u32 dlcEntitlementCount;              // +0x120
    std::array<u8, 0x4E> unknown_124;
    u8 freshModeEnabled;                  // +0x172
};

struct CC_CharacterSyncPacketLayout {
    std::size_t packetSize = 0x430;
    std::size_t baseCharacterIdsOffset = 0x008;
    std::size_t firstUsableBaseCharacterIdOffset = 0x010;
    std::size_t baseCharacterIdBytes = 0x108;
    std::size_t playerMaskOffset = 0x110;
    std::size_t selectedWorkshopIdsOffset = 0x118;
    std::size_t selectedWorkshopIdBytes = 0x50;
    std::size_t baseCharacterFlagsOffset = 0x168;
    std::size_t firstUsableBaseCharacterFlagOffset = 0x169;
    std::size_t selectedWorkshopFlagsOffset = 0x189;
    std::size_t secondaryCharacterIdsOffset = 0x198;
    std::size_t firstUsableSecondaryCharacterIdOffset = 0x1A0;
    std::size_t perPlayerWorkshopIdsOffset = 0x2A0;
    std::size_t perPlayerWorkshopIdBytes = 0x140;
    std::size_t perPlayerWorkshopFlagsOffset = 0x401;
};

#pragma pack(pop)

static_assert(sizeof(CC_GlobalData) == 0x40);
static_assert(sizeof(CC_CharacterData) == 0x30);
static_assert(sizeof(CC_SaveFile) == 0x850);
static_assert(sizeof(CC_OwnedBuffer20) == 0x14);
static_assert(sizeof(CC_LobbyWorkshopSaveSlot32) == 0x20);
static_assert(sizeof(CC_CharacterRuntimeNode) == 0xC8);
static_assert(sizeof(CC_CharacterTableEntry) == 0x1090);
static_assert(offsetof(CC_SaveFile, global) == 0x000);
static_assert(offsetof(CC_SaveFile, characters) == 0x040);
static_assert(offsetof(CC_SaveFile, keybinds) == 0x820);
static_assert(offsetof(CC_SaveFile, checksum) == 0x84C);
static_assert(offsetof(CC_CharacterData, unlocked) == 0x00);
static_assert(offsetof(CC_CharacterData, weapon) == 0x06);
static_assert(offsetof(CC_CharacterData, animal) == 0x07);
static_assert(offsetof(CC_CharacterData, insane_mode) == 0x17);
static_assert(offsetof(CC_CharacterData, skull) == 0x1B);
static_assert(offsetof(CC_ProfileManagerPartial, dlcEntitlementTable) == 0x118);
static_assert(offsetof(CC_ProfileManagerPartial, dlcEntitlementCount) == 0x120);
static_assert(offsetof(CC_ProfileManagerPartial, freshModeEnabled) == 0x172);

} // namespace HookCrashers::NativeDB
