#include "SavePatches.h"
#include "CharacterConfig.h"
#include "../Util/MemoryPatcher.h"
#include "../Util/Logger.h"

namespace HookCrashers::Save {
	namespace {
		constexpr int kSavePatchPhase = 7;
		constexpr bool kPhase4EnableValidateAndRevokeDLC = true;
		constexpr bool kPhase4ExpandValidateAndRevokeDLCScan = true;
		constexpr bool kPhase4SkipValidateAndRevokeDLCRebuild = false;
		constexpr bool kPhase4EnableIsCharacterUnlockedForPlayer = true;
	}

	bool ApplySaveExpansionPatches() {
		int N = CharacterConfig::Instance().GetAddonCount();
		Util::Logger::Instance().Get()->info("[Save] Applying save expansion patches. phase={} addon_count={}.", kSavePatchPhase, N);
		if (N <= 0) {
			Util::Logger::Instance().Get()->info("[Save] No addon characters registered; save/lobby expansion patches are skipped and vanilla character/workshop behavior is preserved.");
			return true;
		}

		// ============================================================
		//  Derived sizes
		// ============================================================
		int total = TOTAL_ORIGINAL_SLOTS + N;
		int split = BASE_FRESH_COUNT + 1 + N;           // base/workshop split (0x21 + N)
		int tableBytes = total * 4;        // memset size for slot table
		int newSize = BASE_SAVE_SIZE + CHAR_DATA_SIZE * N;   // save buffer size     (2128 + 48*N)
		int newCapacity = BASE_SAVE_CAPACITY + CHAR_DATA_SIZE * N;   // save buffer capacity (2124 + 48*N)
		int addonBytes = CHAR_DATA_SIZE * N;
		int totalBase = BASE_CHAR_COUNT + N;           // base char count + addons
		int newKeybindsLimit = BASE_KEYBINDS_OFFSET + addonBytes; // keybinds offset in buffer
		int newCharacterTableBufferSize = CHAR_TABLE_BASE_SIZE + CHAR_TABLE_ENTRY_SIZE * N; // d_characterTable malloc size
		int keybindsOffset = GLOBAL_UNLOCK_SIZE + (TOTAL_ORIGINAL_CHARS + N) * CHAR_DATA_SIZE;

		// Network
		int p2Start = BASE_CHAR_COUNT + WORKSHOP_CHAR_COUNT + N;
		int p1WorkshopStart = totalBase;
		int safeLimit = p2Start - 1;

		// Network Packets
		int workshopIndex = 1131 + totalBase; // Indice (es. 1163 + N)
		int workshopByteOffset = workshopIndex * 4; // Offset in byte (es. 122Ch + N*4)
		int workshopSkinEntryIndex = 1130 + split; // First 40 workshop skin entries in skinEntryByIndex.
		int workshopSkinEntryByteOffset = workshopSkinEntryIndex * 4;
		int packetFlagOffset = 352 + (N * 8); // Gli ID occupano 8 byte, quindi i flag slittano di 8*N
		int syncObjFlagOffset = 992 + (N * 8); // Anche nella memoria interna i flag slittano
		int memcpyDwords = 100 + (N * 2);      // Ogni personaggio aggiunge 2 DWORD (8 byte) al pacchetto
		int selected10Index = 1306 + N;
		int selected10ByteOffset = selected10Index * 4;
		int workshopSelectionStart = totalBase; // Default 32 (0x20), becomes 32 + addonCount.
		int firstPacketBlockBytes = 0x108 + (N * 8);
		int firstPacketCopyDwords = 0x42 + (N * 2);
		int secondPacketOffset = 0x198 + (N * 8);
		int workshopIdsOffset = 0x110 + (N * 8);
		int workshopFlagsOffset = 0x188 + N;
		int secondPacketFlagsOffset = 0x400 + N;
		int secondPacketTailOffset = 0x401 + N;
		int finalPacketOffset = 0x2A0 + (N * 8);
		int copyTailSrc0 = 0x160 + (N * 8);
		int copyTailDst0 = 0x3E0 + (N * 8);
		int copyTailSrc1 = 0x170 + (N * 8);
		int copyTailDst1 = 0x3F0 + (N * 8);
		int copyTailSrcByte = 0x180 + (N * 8);
		int copyTailDstByte = 0x400 + (N * 8);

		Util::Logger::Instance().Get()->info(
			"[NetworkPatch] addon_count={} split={} total={} first_packet_bytes={} first_copy_dwords={} memcpy_dwords={} sync_flags=0x{:X} ws_ids=0x{:X} ws_flags=0x{:X} second_offset=0x{:X} final_offset=0x{:X}.",
			N,
			split,
			total,
			firstPacketBlockBytes,
			firstPacketCopyDwords,
			memcpyDwords,
			syncObjFlagOffset,
			workshopIdsOffset,
			workshopFlagsOffset,
			secondPacketOffset,
			finalPacketOffset);

		Util::Logger::Instance().Get()->info(
			"[Save] Derived expansion values total_slots={} split_slots={} table_bytes={} save_size={} save_capacity={} addon_bytes={} total_base_chars={} keybind_limit={} char_table_size={} keybinds_offset={} network_p2_start={} workshop_start={} network_safe_limit={} workshop_index={} workshop_byte_offset={} packet_flag_offset={} sync_flag_offset={} memcpy_dwords={} workshop_selection_start={} first_packet_bytes={} first_packet_copy_dwords={} second_packet_offset={} workshop_ids_offset={} workshop_flags_offset={} second_packet_flags_offset={} second_packet_tail_offset={} final_packet_offset={}.",
			total,
			split,
			tableBytes,
			newSize,
			newCapacity,
			addonBytes,
			totalBase,
			newKeybindsLimit,
			newCharacterTableBufferSize,
			keybindsOffset,
			p2Start,
			p1WorkshopStart,
			safeLimit,
			workshopIndex,
			workshopByteOffset,
			packetFlagOffset,
			syncObjFlagOffset,
			memcpyDwords,
			workshopSelectionStart,
			firstPacketBlockBytes,
			firstPacketCopyDwords,
			secondPacketOffset,
			workshopIdsOffset,
			workshopFlagsOffset,
			secondPacketFlagsOffset,
			secondPacketTailOffset,
			finalPacketOffset);

		// ============================================================
		//  1. SAVE FILE - BUFFER ALLOCATION
		//     IsSaveBufferUninitialized
		// ============================================================

		// Malloc(v2, 2128) -> newSize
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xFB2C5 + 1, {
			(uint8_t)(newSize),
			(uint8_t)(newSize >> 8),
			(uint8_t)(newSize >> 16),
			(uint8_t)(newSize >> 24)
			});
		// FastMemset(v3, 0, 2128) -> newSize
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xFB2EF + 1, {
			(uint8_t)(newSize),
			(uint8_t)(newSize >> 8),
			(uint8_t)(newSize >> 16),
			(uint8_t)(newSize >> 24)
			});
		// this[2] = 2124 -> newCapacity
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xFB2F7 + 3, {
			(uint8_t)(newCapacity),
			(uint8_t)(newCapacity >> 8),
			(uint8_t)(newCapacity >> 16),
			(uint8_t)(newCapacity >> 24)
			});

		// ============================================================
		//  2. SAVE FILE - VALIDATION BYPASSES
		// ============================================================

		// ValidateFileSaveSize -> always return 1
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12EDB0, { 0xB0, 0x01, 0xC3, 0x90, 0x90, 0x90 });

		// sub_8C7020 (backup gen) - skip size check 2128/1600/1552
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xE70B3, { 0xEB, 0x2B, 0x90, 0x90, 0x90, 0x90 });

		// sub_90EDD0 - cmp edx, 2128
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12EE06 + 2, {
			(uint8_t)(newSize),
			(uint8_t)(newSize >> 8),
			(uint8_t)(newSize >> 16),
			(uint8_t)(newSize >> 24)
			});

		// ============================================================
		//  3. KEYBINDS - CURSOR MANAGEMENT
		//     Pattern: if (buffer.size > 0x820) buffer.cursor = 0x820
		//     Both CMP and MOV patched with newKeybindsLimit = 0x820 + 48*N
		// ============================================================

		// sub_90D0B0 - LoadKeybindsFromBuffer
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12D344 + 3, {
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12D34D + 3, {
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});

		// GenerateDefaultKeybinds
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12CCDC + 1, {
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12CCE3 + 3, {
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});

		// sub_91B020 - ACCEPT keybind
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x13B050 + 3, {
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x13B059 + 3, {
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});

		// sub_91B4E0 - WRITE keybind (exit settings)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x13B53B + 3, {
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x13B544 + 3, {
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});

		// sub_91BFA0 - REVERT keybind / open settings display
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x13BFD2 + 3, {
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x13BFDB + 3, {
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});

		// sub_90CC40 - CheckKeybindsZero (calls GenerateDefaultKeybinds if all zero)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12CC55 + 1, {
			(uint8_t)(0x832 + addonBytes),
			(uint8_t)((0x832 + addonBytes) >> 8),
			(uint8_t)((0x832 + addonBytes) >> 16),
			(uint8_t)((0x832 + addonBytes) >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12CC5C + 3, {
			(uint8_t)(0x832 + addonBytes),
			(uint8_t)((0x832 + addonBytes) >> 8),
			(uint8_t)((0x832 + addonBytes) >> 16),
			(uint8_t)((0x832 + addonBytes) >> 24)
			});

		// ============================================================
		//  4. CHARACTER DATA - DEFAULT CHARACTERS
		//     CreateDefaultCharacters loop limit (0x2A = 42 chars)
		// ============================================================

		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12D05B + 3, { (uint8_t)(TOTAL_ORIGINAL_CHARS + N) });

		// sub_90C9C0 - ValidateCharacterStats loop limit (i < 0x821 -> i < 0x821 + addonBytes)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12CC22 + 2, {
			(uint8_t)(0x821 + addonBytes),
			(uint8_t)((0x821 + addonBytes) >> 8),
			(uint8_t)((0x821 + addonBytes) >> 16),
			(uint8_t)((0x821 + addonBytes) >> 24)
			});
		if (kSavePatchPhase <= 1) {
			Util::Logger::Instance().Get()->info(
				"[Save] Phase 1 save-only expansion patches applied. final_save_size={} final_save_capacity={} expanded_characters={} safe_characters={}.",
				newSize,
				newCapacity,
				totalBase,
				total);
			return true;
		}

		// ============================================================
		//  5. CHARACTER TABLE (LobbyManager)
		//     d_characterTable allocation and slot table setup
		// ============================================================
		// LobbyManagerConstructor - slot table size
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x830FE + 2, { (uint8_t)total });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x83103 + 1, { (uint8_t)total });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8313F + 1, { (uint8_t)(tableBytes & 0xFF), (uint8_t)(tableBytes >> 8) });

		// d_characterTable malloc size
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x10A684 + 1, {
			(uint8_t)(newCharacterTableBufferSize),
			(uint8_t)(newCharacterTableBufferSize >> 8),
			(uint8_t)(newCharacterTableBufferSize >> 16),
			(uint8_t)(newCharacterTableBufferSize >> 24)
			});
		if (kSavePatchPhase <= 2) {
			Util::Logger::Instance().Get()->info(
				"[Save] Phase 2 constructor/table allocation patches applied. final_save_size={} final_save_capacity={} expanded_characters={} safe_characters={}.",
				newSize,
				newCapacity,
				totalBase,
				total);
			return true;
		}

		// RebuildCharacterSlotTable
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x84760 + 1, { (uint8_t)(tableBytes & 0xFF), (uint8_t)(tableBytes >> 8) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8481B + 2, { (uint8_t)split });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x84A8A + 2, { (uint8_t)total });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x84830 + 2, { (uint8_t)(-totalBase) });
		if (kSavePatchPhase <= 3) {
			Util::Logger::Instance().Get()->info(
				"[Save] Phase 3 rebuild patches applied. final_save_size={} final_save_capacity={} expanded_characters={} safe_characters={}.",
				newSize,
				newCapacity,
				totalBase,
				total);
			return true;
		}

		// ============================================================
		//  6. CHARACTER UNLOCK / SKIN LOGIC
		// ============================================================

		// DisableInvalidOrUnavailableDLCCharacters
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8EA00 + 2, { (uint8_t)(total) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8EA22 + 2, { (uint8_t)(split) });

		// ValidateAndRevokeDLC
		if (kPhase4EnableValidateAndRevokeDLC) {
			if (kPhase4ExpandValidateAndRevokeDLCScan) {
				HookCrashers::Util::MemoryPatcher::PatchBytes(0x8744F + 2, { (uint8_t)total });
			}
			if (kPhase4SkipValidateAndRevokeDLCRebuild) {
				HookCrashers::Util::MemoryPatcher::PatchBytes(0x8732B, { 0x90, 0x90, 0x90, 0x90, 0x90 });
			}
		}
		if (!kPhase4EnableIsCharacterUnlockedForPlayer) {
			Util::Logger::Instance().Get()->info(
				"[Save] Phase 4B unlock patches disabled for validation. validate_enabled={} is_unlocked_enabled={} final_save_size={} final_save_capacity={} expanded_characters={} safe_characters={}.",
				kPhase4EnableValidateAndRevokeDLC,
				kPhase4EnableIsCharacterUnlockedForPlayer,
				newSize,
				newCapacity,
				totalBase,
				total);
			return true;
		}

		// IsCharacterUnlockedForPlayer
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x10354A + 2, { (uint8_t)(totalBase - 1) });               // Default 31 (0x1F)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x103573 + 2, { (uint8_t)(-totalBase) });                  // Default -32 (0xE0)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x1035A9 + 2, { (uint8_t)(totalBase + 1) });               // Default 33 (0x21)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x102CD7 + 0x8E0 + 2, { (uint8_t)(totalBase + 10 + 1) }); // Default 43 (0x2B)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x102CE5 + 0x8E0 + 2, { (uint8_t)(totalBase + 20 + 1) }); // Default 53 (0x35)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x102CF3 + 0x8E0 + 2, { (uint8_t)(totalBase + 30 + 1) }); // Default 63 (0x3F)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x102CDC + 0x8E0 + 2, { (uint8_t)(totalBase + 20 + 1) }); // Default 53 (0x35)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x102CEA + 0x8E0 + 2, { (uint8_t)(totalBase + 30 + 1) }); // Default 63 (0x3F)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x102CFC + 0x8E0 + 2, { (uint8_t)(total) });              // Default 73 (0x49)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x102CCE + 0x8E0 + 2, { (uint8_t)(totalBase + 10 + 1) }); // Default 43 (0x2B)
		if (kSavePatchPhase <= 4) {
			Util::Logger::Instance().Get()->info(
				"[Save] Phase 4 unlock validation patches applied. final_save_size={} final_save_capacity={} expanded_characters={} safe_characters={}.",
				newSize,
				newCapacity,
				totalBase,
				total);
			return true;
		}

		// AdvanceToNextSelectableCharacter (module base 0x7E0000, function 0x92AD20)
		// Keep the fresh-only gate at index 31 (0x1F), but move the workshop/save split
		// from 32 (0x20) to 32 + addonCount so addon characters continue through save-side
		// unlock scanning before workshop slot logic runs.
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x14AD9E + 2, { (uint8_t)(workshopSelectionStart) }); // cmp eax, 0x20
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x14ADBB + 2, { (uint8_t)(workshopSelectionStart) }); // sub eax, 0x20
		if (kSavePatchPhase <= 5) {
			Util::Logger::Instance().Get()->info(
				"[Save] Phase 5 selectable character patches applied. final_save_size={} final_save_capacity={} expanded_characters={} safe_characters={}.",
				newSize,
				newCapacity,
				totalBase,
				total);
			return true;
		}

		// AttachSkinGraphic
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8D683 + 2, { (uint8_t)(totalBase + 1) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8D652 + 2, { (uint8_t)(total) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8D659 + 2, { (uint8_t)(total) });

		// AttachWorkshopSkin
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8D29A + 2, { (uint8_t)(totalBase + 1) });
		if (kSavePatchPhase <= 6) {
			Util::Logger::Instance().Get()->info(
				"[Save] Phase 6 attach skin patches applied. final_save_size={} final_save_capacity={} expanded_characters={} safe_characters={}.",
				newSize,
				newCapacity,
				totalBase,
				total);
			return true;
		}

		// ============================================================
		//  7. NETWORK CHARACTERS
		// ============================================================
		// Network byte patches are disabled: vanilla workshop multiplayer sync is
		// known-good, while the expanded network guards still crash with addon
		// characters. Keep networking on the vanilla path until it is moved to
		// targeted detours.
		Util::Logger::Instance().Get()->info("[NetworkPatch] Network byte patches are disabled; using vanilla multiplayer workshop sync path.");

		// BuildCharacterSyncPacket still needs to read the 40 workshop skin entries
		// from the shifted skinEntryByIndex range. This does not change packet
		// layout; it only moves the source table from slot 33 to 33 + addonCount.
		Util::Logger::Instance().Get()->info(
			"[NetworkPatch] Aligning workshop skin entry source for clone slot. index={} byte_offset=0x{:X}.",
			workshopSkinEntryIndex,
			workshopSkinEntryByteOffset);
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x88D60 + 2, {
			(uint8_t)(workshopSkinEntryByteOffset),
			(uint8_t)(workshopSkinEntryByteOffset >> 8),
			(uint8_t)(workshopSkinEntryByteOffset >> 16),
			(uint8_t)(workshopSkinEntryByteOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x890BD + 1, {
			(uint8_t)(workshopSkinEntryIndex),
			(uint8_t)(workshopSkinEntryIndex >> 8),
			(uint8_t)(workshopSkinEntryIndex >> 16),
			(uint8_t)(workshopSkinEntryIndex >> 24)
			});

#if 0
		// IsCharacterIndexValid
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8E726 + 2, { (uint8_t)(total) });

		// IsCharacterDLCOwned
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8E7F3 + 2, { (uint8_t)(total) });

		// ValidateReadySkinsForPlayers
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x87E8A + 1, { (uint8_t)(split) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x88035 + 2, { (uint8_t)(split) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x87FB6 + 2, { (uint8_t)(split) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8807E + 2, { (uint8_t)(total) });

		// IsCharacterDLCValid
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8E886 + 2, { (uint8_t)(total) });

		// LobbyTrySelectChar
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x103C84 + 2, { (uint8_t)(split) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x103CF4 + 2, { (uint8_t)(split) });

		// ConsumeCharacterDLCSlot
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8E8CD + 2, { (uint8_t)(total) });

		// LobbySkinCharAvail
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x103DD7 + 2, { (uint8_t)(split) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x103E28 + 2, { (uint8_t)(split) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x103F1E + 2, { (uint8_t)(split) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x103F34 + 2, { (uint8_t)(-split) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x103F23 + 2, { (uint8_t)(-split) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x103F26 + 1, { (uint8_t)(WORKSHOP_CHAR_COUNT) });

		// FindNextFreeWorkshopSlot
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8E783 + 2, { (uint8_t)(total) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8E792 + 1, { (uint8_t)(WORKSHOP_CHAR_COUNT) });

		// SetupSkinPartGraphic
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8E37A + 7, { (uint8_t)(split) });

		// UpdateCurrentCharacterSelection
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x3C9AA + 2, { (uint8_t)(total) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x3C9AF + 1, { (uint8_t)(split) });

		// SyncWorkshopCharacterFromSave?
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x87A79 + 2, { (uint8_t)(total) }); // Leaving local lobby result in "You got kicked"
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x87810 + 2, { (uint8_t)(split) }); // Leaving local lobby result in "You got kicked"

		// TryRevokeDLCCharacter
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x897E6 + 2, { (uint8_t)(total) });

		// MergeCharacterSyncData
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FFDA + 2, {(uint8_t)(split)});

		// sub_83FEE0
		// Merge workshop payload blocks from the vanilla/fresh packet into the remote workshop packet area.
		/*int wsMaskOffset = 0x108 + (N * 8);
		int wsIDsOffset = 0x110 + (N * 8);
		int wsFlagsSrc = 0x181 + (N * 9);
		int wsFlagsDest = 0x401 + (N * 9);
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FEEC + 2, {
			(uint8_t)(wsMaskOffset),
			(uint8_t)(wsMaskOffset >> 8),
			(uint8_t)(wsMaskOffset >> 16),
			(uint8_t)(wsMaskOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FF1D + 2, {
			(uint8_t)(wsIDsOffset),
			(uint8_t)(wsIDsOffset >> 8),
			(uint8_t)(wsIDsOffset >> 16),
			(uint8_t)(wsIDsOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FF35 + 2, {
			(uint8_t)(finalPacketOffset),
			(uint8_t)(finalPacketOffset >> 8),
			(uint8_t)(finalPacketOffset >> 16),
			(uint8_t)(finalPacketOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FF3B + 2, {
			(uint8_t)(finalPacketOffset + 4),
			(uint8_t)((finalPacketOffset + 4) >> 8),
			(uint8_t)((finalPacketOffset + 4) >> 16),
			(uint8_t)((finalPacketOffset + 4) >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FF46 + 2, {
			(uint8_t)(finalPacketOffset + 4),
			(uint8_t)((finalPacketOffset + 4) >> 8),
			(uint8_t)((finalPacketOffset + 4) >> 16),
			(uint8_t)((finalPacketOffset + 4) >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FF4F + 2, {
			(uint8_t)(finalPacketOffset),
			(uint8_t)(finalPacketOffset >> 8),
			(uint8_t)(finalPacketOffset >> 16),
			(uint8_t)(finalPacketOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FF5B + 3, {
			(uint8_t)(wsFlagsSrc),
			(uint8_t)(wsFlagsSrc >> 8),
			(uint8_t)(wsFlagsSrc >> 16),
			(uint8_t)(wsFlagsSrc >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FF62 + 3, {
			(uint8_t)(wsFlagsDest),
			(uint8_t)(wsFlagsDest >> 8),
			(uint8_t)(wsFlagsDest >> 16),
			(uint8_t)(wsFlagsDest >> 24)
			});

		// ApplyCharacterSyncPacket
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x87810 + 2, { (uint8_t)(split) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8782B + 3, {
			(uint8_t)(syncObjFlagOffset),
			(uint8_t)(syncObjFlagOffset >> 8),
			(uint8_t)(syncObjFlagOffset >> 16),
			(uint8_t)(syncObjFlagOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x87A79 + 2, { (uint8_t)(total) });

		// CopyVanillaBlocks
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FDD4 + 1, {
			(uint8_t)(firstPacketCopyDwords),
			(uint8_t)(firstPacketCopyDwords >> 8),
			(uint8_t)(firstPacketCopyDwords >> 16),
			(uint8_t)(firstPacketCopyDwords >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FDE7 + 3, {
			(uint8_t)(copyTailSrc0),
			(uint8_t)(copyTailSrc0 >> 8),
			(uint8_t)(copyTailSrc0 >> 16),
			(uint8_t)(copyTailSrc0 >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FDF0 + 3, {
			(uint8_t)(copyTailDst0),
			(uint8_t)(copyTailDst0 >> 8),
			(uint8_t)(copyTailDst0 >> 16),
			(uint8_t)(copyTailDst0 >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FDF7 + 3, {
			(uint8_t)(copyTailSrc1),
			(uint8_t)(copyTailSrc1 >> 8),
			(uint8_t)(copyTailSrc1 >> 16),
			(uint8_t)(copyTailSrc1 >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FDFE + 3, {
			(uint8_t)(copyTailDst1),
			(uint8_t)(copyTailDst1 >> 8),
			(uint8_t)(copyTailDst1 >> 16),
			(uint8_t)(copyTailDst1 >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FE05 + 2, {
			(uint8_t)(copyTailSrcByte),
			(uint8_t)(copyTailSrcByte >> 8),
			(uint8_t)(copyTailSrcByte >> 16),
			(uint8_t)(copyTailSrcByte >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FE0B + 2, {
			(uint8_t)(copyTailDstByte),
			(uint8_t)(copyTailDstByte >> 8),
			(uint8_t)(copyTailDstByte >> 16),
			(uint8_t)(copyTailDstByte >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FE1B + 1, {
			(uint8_t)(memcpyDwords),
			(uint8_t)(memcpyDwords >> 8),
			(uint8_t)(memcpyDwords >> 16),
			(uint8_t)(memcpyDwords >> 24)
			});

		// ClearCharacterSyncPacket
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FD03 + 1, {
			(uint8_t)(firstPacketBlockBytes),
			(uint8_t)(firstPacketBlockBytes >> 8),
			(uint8_t)(firstPacketBlockBytes >> 16),
			(uint8_t)(firstPacketBlockBytes >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FD1B + 3, {
			(uint8_t)(workshopIdsOffset),
			(uint8_t)(workshopIdsOffset >> 8),
			(uint8_t)(workshopIdsOffset >> 16),
			(uint8_t)(workshopIdsOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FD33 + 3, {
			(uint8_t)(workshopFlagsOffset),
			(uint8_t)(workshopFlagsOffset >> 8),
			(uint8_t)(workshopFlagsOffset >> 16),
			(uint8_t)(workshopFlagsOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FD3B + 3, {
			(uint8_t)(workshopIdsOffset),
			(uint8_t)(workshopIdsOffset >> 8),
			(uint8_t)(workshopIdsOffset >> 16),
			(uint8_t)(workshopIdsOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FD47 + 1, {
			(uint8_t)(firstPacketBlockBytes),
			(uint8_t)(firstPacketBlockBytes >> 8),
			(uint8_t)(firstPacketBlockBytes >> 16),
			(uint8_t)(firstPacketBlockBytes >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FD4F + 3, {
			(uint8_t)(secondPacketOffset),
			(uint8_t)(secondPacketOffset >> 8),
			(uint8_t)(secondPacketOffset >> 16),
			(uint8_t)(secondPacketOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FD57 + 3, {
			(uint8_t)(secondPacketTailOffset),
			(uint8_t)(secondPacketTailOffset >> 8),
			(uint8_t)(secondPacketTailOffset >> 16),
			(uint8_t)(secondPacketTailOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FD60 + 3, {
			(uint8_t)(secondPacketTailOffset + 8),
			(uint8_t)((secondPacketTailOffset + 8) >> 8),
			(uint8_t)((secondPacketTailOffset + 8) >> 16),
			(uint8_t)((secondPacketTailOffset + 8) >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FD71 + 3, {
			(uint8_t)(finalPacketOffset),
			(uint8_t)(finalPacketOffset >> 8),
			(uint8_t)(finalPacketOffset >> 16),
			(uint8_t)(finalPacketOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FD7C + 3, {
			(uint8_t)(copyTailDst0),
			(uint8_t)(copyTailDst0 >> 8),
			(uint8_t)(copyTailDst0 >> 16),
			(uint8_t)(copyTailDst0 >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FD85 + 3, {
			(uint8_t)(copyTailDst1),
			(uint8_t)(copyTailDst1 >> 8),
			(uint8_t)(copyTailDst1 >> 16),
			(uint8_t)(copyTailDst1 >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FD8D + 2, {
			(uint8_t)(secondPacketFlagsOffset),
			(uint8_t)(secondPacketFlagsOffset >> 8),
			(uint8_t)(secondPacketFlagsOffset >> 16),
			(uint8_t)(secondPacketFlagsOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FD9F + 3, {
			(uint8_t)(secondPacketTailOffset),
			(uint8_t)(secondPacketTailOffset >> 8),
			(uint8_t)(secondPacketTailOffset >> 16),
			(uint8_t)(secondPacketTailOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FDA8 + 3, {
			(uint8_t)(secondPacketTailOffset + 0x10),
			(uint8_t)((secondPacketTailOffset + 0x10) >> 8),
			(uint8_t)((secondPacketTailOffset + 0x10) >> 16),
			(uint8_t)((secondPacketTailOffset + 0x10) >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FDAF + 3, {
			(uint8_t)(secondPacketTailOffset + 0x20),
			(uint8_t)((secondPacketTailOffset + 0x20) >> 8),
			(uint8_t)((secondPacketTailOffset + 0x20) >> 16),
			(uint8_t)((secondPacketTailOffset + 0x20) >> 24)
			});

		// sub_8D0750 - build temporary workshop sync packet for the remote clone slot.
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xF08FA + 4, {
			(uint8_t)(finalPacketOffset),
			(uint8_t)(finalPacketOffset >> 8),
			(uint8_t)(finalPacketOffset >> 16),
			(uint8_t)(finalPacketOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xF0901 + 4, {
			(uint8_t)(finalPacketOffset + 4),
			(uint8_t)((finalPacketOffset + 4) >> 8),
			(uint8_t)((finalPacketOffset + 4) >> 16),
			(uint8_t)((finalPacketOffset + 4) >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xF091E + 3, {
			(uint8_t)(workshopIdsOffset),
			(uint8_t)(workshopIdsOffset >> 8),
			(uint8_t)(workshopIdsOffset >> 16),
			(uint8_t)(workshopIdsOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xF092A + 4, {
			(uint8_t)(workshopIdsOffset + 8),
			(uint8_t)((workshopIdsOffset + 8) >> 8),
			(uint8_t)((workshopIdsOffset + 8) >> 16),
			(uint8_t)((workshopIdsOffset + 8) >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xF0934 + 4, {
			(uint8_t)(workshopIdsOffset + 12),
			(uint8_t)((workshopIdsOffset + 12) >> 8),
			(uint8_t)((workshopIdsOffset + 12) >> 16),
			(uint8_t)((workshopIdsOffset + 12) >> 24)
			});*/

		// Minimal workshop network packet alignment.
		// Keep large packet copy/clear rewrites disabled while validating the remote workshop slot path.
		int wsMaskOffset = 0x108 + (N * 8);
		int wsIDsOffset = 0x110 + (N * 8);
		int wsFlagsSrc = 0x181 + N;
		int wsFlagsDest = 0x401 + N;
		Util::Logger::Instance().Get()->info(
			"[NetworkPatch] Minimal workshop sync enabled. apply_flags=0x{:X} merge_mask=0x{:X} merge_ids=0x{:X} merge_src_flags=0x{:X} merge_dst_flags=0x{:X} final_packet=0x{:X}.",
			syncObjFlagOffset,
			wsMaskOffset,
			wsIDsOffset,
			wsFlagsSrc,
			wsFlagsDest,
			finalPacketOffset);

		// ApplyCharacterSyncPacket: read shifted fresh/workshop flags from the expanded sync packet.
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8782B + 3, {
			(uint8_t)(syncObjFlagOffset),
			(uint8_t)(syncObjFlagOffset >> 8),
			(uint8_t)(syncObjFlagOffset >> 16),
			(uint8_t)(syncObjFlagOffset >> 24)
			});

		// sub_83FEE0: merge 10 workshop identities/flags from the shifted packet layout.
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FEEC + 2, {
			(uint8_t)(wsMaskOffset),
			(uint8_t)(wsMaskOffset >> 8),
			(uint8_t)(wsMaskOffset >> 16),
			(uint8_t)(wsMaskOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FF1D + 2, {
			(uint8_t)(wsIDsOffset),
			(uint8_t)(wsIDsOffset >> 8),
			(uint8_t)(wsIDsOffset >> 16),
			(uint8_t)(wsIDsOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FF35 + 2, {
			(uint8_t)(finalPacketOffset),
			(uint8_t)(finalPacketOffset >> 8),
			(uint8_t)(finalPacketOffset >> 16),
			(uint8_t)(finalPacketOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FF3B + 2, {
			(uint8_t)(finalPacketOffset + 4),
			(uint8_t)((finalPacketOffset + 4) >> 8),
			(uint8_t)((finalPacketOffset + 4) >> 16),
			(uint8_t)((finalPacketOffset + 4) >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FF46 + 2, {
			(uint8_t)(finalPacketOffset + 4),
			(uint8_t)((finalPacketOffset + 4) >> 8),
			(uint8_t)((finalPacketOffset + 4) >> 16),
			(uint8_t)((finalPacketOffset + 4) >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FF4F + 2, {
			(uint8_t)(finalPacketOffset),
			(uint8_t)(finalPacketOffset >> 8),
			(uint8_t)(finalPacketOffset >> 16),
			(uint8_t)(finalPacketOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FF5B + 3, {
			(uint8_t)(wsFlagsSrc),
			(uint8_t)(wsFlagsSrc >> 8),
			(uint8_t)(wsFlagsSrc >> 16),
			(uint8_t)(wsFlagsSrc >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x5FF62 + 3, {
			(uint8_t)(wsFlagsDest),
			(uint8_t)(wsFlagsDest >> 8),
			(uint8_t)(wsFlagsDest >> 16),
			(uint8_t)(wsFlagsDest >> 24)
			});

		// sub_8D0750: temporary packet used to materialize the remote workshop clone slot.
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xF08FA + 4, {
			(uint8_t)(finalPacketOffset),
			(uint8_t)(finalPacketOffset >> 8),
			(uint8_t)(finalPacketOffset >> 16),
			(uint8_t)(finalPacketOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xF0901 + 4, {
			(uint8_t)(finalPacketOffset + 4),
			(uint8_t)((finalPacketOffset + 4) >> 8),
			(uint8_t)((finalPacketOffset + 4) >> 16),
			(uint8_t)((finalPacketOffset + 4) >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xF091E + 3, {
			(uint8_t)(workshopIdsOffset),
			(uint8_t)(workshopIdsOffset >> 8),
			(uint8_t)(workshopIdsOffset >> 16),
			(uint8_t)(workshopIdsOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xF092A + 4, {
			(uint8_t)(workshopIdsOffset + 8),
			(uint8_t)((workshopIdsOffset + 8) >> 8),
			(uint8_t)((workshopIdsOffset + 8) >> 16),
			(uint8_t)((workshopIdsOffset + 8) >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xF0934 + 4, {
			(uint8_t)(workshopIdsOffset + 12),
			(uint8_t)((workshopIdsOffset + 12) >> 8),
			(uint8_t)((workshopIdsOffset + 12) >> 16),
			(uint8_t)((workshopIdsOffset + 12) >> 24)
			});

		// BuildCharacterSyncPacket: source the 40 workshop skin entries from the shifted split.
		Util::Logger::Instance().Get()->info(
			"[NetworkPatch] BuildCharacterSyncPacket workshop skin entries index={} byte_offset=0x{:X}.",
			workshopSkinEntryIndex,
			workshopSkinEntryByteOffset);
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x88D60 + 2, {
			(uint8_t)(workshopSkinEntryByteOffset),
			(uint8_t)(workshopSkinEntryByteOffset >> 8),
			(uint8_t)(workshopSkinEntryByteOffset >> 16),
			(uint8_t)(workshopSkinEntryByteOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x890BD + 1, {
			(uint8_t)(workshopSkinEntryIndex),
			(uint8_t)(workshopSkinEntryIndex >> 8),
			(uint8_t)(workshopSkinEntryIndex >> 16),
			(uint8_t)(workshopSkinEntryIndex >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x88CD3 + 3, {
			(uint8_t)(wsFlagsDest),
			(uint8_t)(wsFlagsDest >> 8),
			(uint8_t)(wsFlagsDest >> 16),
			(uint8_t)(wsFlagsDest >> 24)
			});
		
		// BuildCharacterSyncPacket
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x88AE3 + 2, {(uint8_t)(split)});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x88AF2 + 2, {
			(uint8_t)(workshopByteOffset & 0xFF),
			(uint8_t)((workshopByteOffset >> 8) & 0xFF),
			0x00, 0x00
			});
#endif
		/*HookCrashers::Util::MemoryPatcher::PatchBytes(0x88D60 + 2, {
			(uint8_t)(selected10ByteOffset & 0xFF),
			(uint8_t)((selected10ByteOffset >> 8) & 0xFF),
			0x00, 0x00
			});*/

		// ResolveWorkshopCharacterConflicts
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8405B + 2, { (uint8_t)(split) });
		
		// GetCurrentWorkshopSkinEntry
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8D29A + 2, { (uint8_t)(split) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8D2CA + 2, { (uint8_t)(split) });

		// GetCharacterSlotName
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8A627 + 2, {(uint8_t)(-split)});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8A633 + 2, { (uint8_t)(split - 1) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8A641 + 3, { (uint8_t)(split - 2) });

		// GetCharacterSlotCheckDefaultName??
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8FCCA + 2, { (uint8_t)(split) });

		// GetCharacterSlotCheckDefaultName2??
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8FD70 + 2, { (uint8_t)(split) });

		// unkf_x
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8A78E + 2, {(uint8_t)(total)});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8EB72 + 2, { (uint8_t)(total) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8EBB4 + 2, { (uint8_t)(total) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8FC54 + 2, { (uint8_t)(total) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8FC63 + 2, { (uint8_t)(total) });
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x87C4A + 2, { (uint8_t)(split) });
		Util::Logger::Instance().Get()->info(
			"[Save] Phase 7 expansion patches applied. final_save_size={} final_save_capacity={} expanded_characters={} safe_characters={}.",
			newSize,
			newCapacity,
			totalBase,
			total);
		return true;
	}
}

		// SetFlagIsCharWorkshop
		// HookCrashers::Util::MemoryPatcher::PatchBytes(0x36986 + 2, { (uint8_t)(totalBase + 1) }); // Default 33 (0x21)

		// GetCharacterGameCompletedForPlayer <- decompile
		//HookCrashers::Util::MemoryPatcher::PatchBytes(0x102B54 + 0x8E0 + 2, { (uint8_t)(-(totalBase + 10)) });
		//HookCrashers::Util::MemoryPatcher::PatchBytes(0x102B64 + 0x8E0 + 2, { (uint8_t)(totalBase) });
		//HookCrashers::Util::MemoryPatcher::PatchBytes(0x102B72 + 0x8E0 + 2, { (uint8_t)(totalBase + 10) });
