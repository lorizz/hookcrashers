#pragma once

#include <cstdint>

namespace HookCrashers::Game {

using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

struct CharacterNetId {
    uint32 lo;
    uint32 hi;
};

struct WorkshopSlotDescriptor {
    uint32 field_00;
    uint32 field_04;
    uint32 field_08;
    uint32 field_0C;
    uint32 field_10;
    uint32 extra_14;
    uint32 extra_18;
    uint32 extra_1C;
};

struct LobbySelectionEntry160 {
    uint8 raw[160];
};

struct LobbyWorkshopEntry64 {
    uint8 raw[64];
};

struct LobbyWorkshopSaveSlot32 {
    uint8 unknown_00[0x14];
    int workshopCharacterIdOrSentinel; // +0x14, -1 means empty in selection logic.
    uint64 presenceValue;              // +0x18, any non-zero value marks the slot populated.
};

struct LobbySkinRect56 {
    int idOrState;
    int field_04;
    int field_08;
    float scaleX;
    float scaleY;
    uint16 field_14;
    uint8 pad_16[34];
};

struct OwnedBuffer20 {
    void* heapData;
    uint32 field_04;
    uint32 length;
    uint32 capacity;
    uint32 inlineThreshold;
};

struct CharacterRuntimeHeader40 {
    uint8 activeFlag;              // +0x00
    uint8 pad_01[0x07];           // +0x01
    uint32 field_08;              // +0x08
    uint32 field_0C;              // +0x0C
    uint32 field_10;              // +0x10
    int workshopCharacterIdOrSentinel; // +0x14
    uint32 workshopIdentityLo;    // +0x18
    uint32 workshopIdentityHi;    // +0x1C
    uint8 pad_20[0x08];           // +0x20
};

struct CharacterNode200 {
    OwnedBuffer20 buffer0;        // +0x00
    uint8 unknown_14[0x04];       // +0x14
    uint32 dword_18;              // +0x18
    uint32 dword_1C;              // +0x1C
    OwnedBuffer20 buffer1;        // +0x20
    uint8 flag_34;                // +0x34
    uint8 flag_35;                // +0x35
    uint8 pad_36[0x02];           // +0x36
    uint8 raw_38_to_AB[0x74];     // +0x38
    uint32 dword_AC;              // +0xAC
    int sentinel_B0;              // +0xB0, initialized to -1 in InitializeCharacterNode
    uint8 unknown_B4[0x04];       // +0xB4
    uint32 dword_B8;              // +0xB8
    uint32 dword_BC;              // +0xBC
    uint32 dword_C0;              // +0xC0
    uint8 flag_C4;                // +0xC4
    uint8 flag_C5;                // +0xC5
    uint8 pad_C6[0x02];           // +0xC6
};

struct CharacterEntry4240 {
    uint8 field_00;                // +0x00
    uint8 pad_01[0x07];            // +0x01
    OwnedBuffer20 buffer08;        // +0x08
    int workshopCharacterIdOrSentinel; // +0x1C, initialized to -1 for workshop entries
    uint32 workshopIdentityLo;     // +0x20
    uint32 workshopIdentityHi;     // +0x24
    CharacterNode200 node28;       // +0x28
    uint8 raw_F0[0xFA0];           // +0xF0
};

struct LobbySelectionState {
    void* ownerOrContext;
    int field_04;
    int field_08;
    int field_0C;
    int field_10;
    int field_14;
    int field_18;
    uint8 field_1C;
    uint8 pad_1D[3];
    int field_20;
    int field_24;
    int field_28;
    int field_2C;
    int field_30;
    uint16 field_34;
    uint8 pad_36[2];
    int field_38;
    int currentVisibleSlot;
    uint16 field_40;
    uint8 pad_42[2];
    int field_44;
    int field_48;
    int field_4C;
    uint8 field_50;
    uint8 pad_51[3];
    int field_54;
    uint16 field_58;
    uint8 pad_5A[2];
    int field_5C;
    int field_60;
    uint8 field_64;
    uint8 pad_65[59];
    LobbySelectionEntry160 entries43[43];
    LobbySelectionEntry160 entries8[8];
    uint8 blob_8300[100];
    uint8 blob_8400[100];
    uint16 field_84FC;
    uint8 pad_214A[2];
    int field_8500;
    char smallBuf_8524[64];
    int field_861C;
    int field_8620;
    int field_8624;
    int field_8628;
    int field_862C;
    char fourccLike_8608[8];
    int field_8630;
    int field_8634;
    int field_8638;
    int field_863C;
    int field_8640;
    int field_8644;
    int field_8648;
    int field_864C;
    uint8 pad_8650[96];
    int field_2192;
    int field_2193;
    uint8 blob_8892[100];
    int ptr_2073;
    int ptr_2074;
    int field_2071;
    int field_2072;
    int helperObjects4[4];
    int helperObjects8[8];
};

struct LobbyManager {
    void* ptr_0000;
    void* ptr_0004;
    void* ptr_0008;
    void* ptr_000C;
    void* ptr_0010;
    void* ptr_0014;
    uint8 pad_0018[32];
    // Confirmed by ReallocCharacterArray(this + 14, count):
    // [14] = begin, [15] = end, [16] = capacityEnd for 0x1090-byte CharacterArrayEntry elements.
    CharacterEntry4240* characterArrayBegin;
    CharacterEntry4240* characterArrayEnd;
    CharacterEntry4240* characterArrayCapacityEnd;
    uint8 pad_0044[4244];
    uint8 state_4312[100];
    uint8 state_4412[100];
    CharacterEntry4240* currentCharacterBase;
    CharacterEntry4240* currentCharacterPtr;
    void* skinEntryByIndex[73];
    uint8 pad_afterSkinEntryByIndex_12CC[92];
    LobbyWorkshopSaveSlot32 workshopSaveSlots10[10]; // +0x1328, used for save-side slots 32..41.
    uint8 pad_afterWorkshopSaveSlots_1468[40];
    uint8 workshopArrayValid;
    uint8 pad_1491[3];
    LobbyWorkshopEntry64 workshopEntries40[40];
    LobbySkinRect56 skinRects40[40];
    uint8 pad_afterSkinRects[4];
    uint8 smallStateA[32];
    uint8 pad_big[4544];
    LobbySelectionState* selectionState;
    void* ptr_14652;
    void* ptr_14656;
    uint32 selectionEnabled;
    uint32 field_14664;
    uint8 field_14668;
    uint8 pad_14669[3];
    uint32 skinScaleOrWidth;
    void* workshopRuntime;
    uint32 field_14680;
    uint32 workshopSyncBusy;
    uint8 pad_tail[32];
    void* ptr_14720;
    void* ptr_14724;
};

struct CharacterSyncPacket {
    uint8 raw[1072];
};

constexpr uintptr_t LOBBY_MANAGER_OFFSET = 0x38E2A8;

#if INTPTR_MAX == INT32_MAX
static_assert(sizeof(CharacterNetId) == 0x8);
static_assert(sizeof(WorkshopSlotDescriptor) == 0x20);
static_assert(sizeof(LobbySelectionEntry160) == 0xA0);
static_assert(sizeof(LobbyWorkshopEntry64) == 0x40);
static_assert(sizeof(LobbyWorkshopSaveSlot32) == 0x20);
static_assert(sizeof(LobbySkinRect56) == 0x38);
static_assert(sizeof(OwnedBuffer20) == 0x14);
static_assert(sizeof(CharacterRuntimeHeader40) == 0x28);
static_assert(sizeof(CharacterNode200) == 0xC8);
static_assert(sizeof(CharacterEntry4240) == 0x1090);
static_assert(sizeof(LobbySelectionState) == 0x22D8);
static_assert(sizeof(LobbyManager) == 0x3988);
static_assert(sizeof(CharacterSyncPacket) == 0x430);
#endif

} // namespace HookCrashers::Game
