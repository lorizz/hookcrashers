#include "SavePatches.h"
#include "CharacterConfig.h"
#include "../Util/MemoryPatcher.h"
#include "../Util/Logger.h"
#include <string>
#include <vector>

namespace HookCrashers::Save {
	namespace {
		constexpr int kSavePatchPhase = 7;
		constexpr bool kPhase4EnableValidateAndRevokeDLC = true;
		constexpr bool kPhase4ExpandValidateAndRevokeDLCScan = true;
		constexpr bool kPhase4SkipValidateAndRevokeDLCRebuild = false;
		constexpr bool kPhase4EnableIsCharacterUnlockedForPlayer = true;
		std::string g_saveName = "mod";
		std::string g_saveFileName = "cc_save_mod.dat";
		std::string g_saveBackupFileName = "cc_save_mod.dat.bak";

		std::string SanitizeSaveName(const std::string& input) {
			std::string out;
			out.reserve(input.size());
			for (char ch : input) {
				const unsigned char c = static_cast<unsigned char>(ch);
				if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
					out.push_back(static_cast<char>(c));
				}
			}
			return out.empty() ? "mod" : out;
		}

		void RefreshSaveFileNames() {
			g_saveFileName = "cc_save_" + g_saveName + ".dat";
			g_saveBackupFileName = g_saveFileName + ".bak";
		}

		std::vector<uint8_t> PointerBytes(const char* ptr) {
			const uintptr_t value = reinterpret_cast<uintptr_t>(ptr);
			return {
				static_cast<uint8_t>(value & 0xFF),
				static_cast<uint8_t>((value >> 8) & 0xFF),
				static_cast<uint8_t>((value >> 16) & 0xFF),
				static_cast<uint8_t>((value >> 24) & 0xFF)
			};
		}

		bool PatchPointerReferences(const std::vector<uintptr_t>& operandRvas, const std::vector<uintptr_t>& pointerRvas, const char* replacement) {
			bool success = true;
			const std::vector<uint8_t> bytes = PointerBytes(replacement);
			for (uintptr_t rva : operandRvas) {
				success = Util::MemoryPatcher::PatchBytes(rva + 1, bytes) && success;
			}
			for (uintptr_t rva : pointerRvas) {
				success = Util::MemoryPatcher::PatchBytes(rva, bytes) && success;
			}
			return success;
		}

		constexpr bool kEnableNetworkPacketExpansionPatches = true;

		std::vector<uint8_t> U32(uint32_t value) {
			return {
				static_cast<uint8_t>(value & 0xFF),
				static_cast<uint8_t>((value >> 8) & 0xFF),
				static_cast<uint8_t>((value >> 16) & 0xFF),
				static_cast<uint8_t>((value >> 24) & 0xFF)
			};
		}

		bool PatchU32(uintptr_t rva, uint32_t value, const char* name) {
			if (rva == 0) {
				Util::Logger::Instance().Get()->error("[NetworkPatch] Missing RVA for {}. Fill the TODO_RVA_* value from IDA before enabling network packet expansion.", name);
				return false;
			}
			return Util::MemoryPatcher::PatchBytes(rva, U32(value));
		}

		bool PatchU8(uintptr_t rva, uint8_t value, const char* name) {
			if (rva == 0) {
				Util::Logger::Instance().Get()->error("[NetworkPatch] Missing RVA for {}. Fill the TODO_RVA_* value from IDA before enabling network packet expansion.", name);
				return false;
			}
			return Util::MemoryPatcher::PatchBytes(rva, { value });
		}

		bool ApplyNetworkPacketExpansionPatches(
			int N,
			int split,
			int total,
			int selected10ByteOffset,
			int workshopSkinEntryIndex,
			int workshopSkinEntryByteOffset,
			int packetFlagOffset,
			int syncObjFlagOffset,
			int firstPacketBlockBytes,
			int secondPacketOffset,
			int workshopIdsOffset,
			int workshopFlagsOffset,
			int secondPacketTailOffset,
			int finalPacketOffset,
			int copyTailSrc0,
			int copyTailDst0,
			int copyTailSrc1,
			int copyTailDst1,
			int copyTailSrcByte,
			int copyTailDstByte) {
			if (!kEnableNetworkPacketExpansionPatches) {
				Util::Logger::Instance().Get()->info("[NetworkPatch] Expanded packet byte patches are disabled. Fill TODO_RVA_* constants and set kEnableNetworkPacketExpansionPatches=true to test multiplayer addon sync.");
				return true;
			}

			// All TODO_RVA_* values are RVAs, not VA addresses. IDA VA -> RVA = VA - 0x100000.
			// Patch address must point at the immediate operand, not always the start of instruction.
			// Example: C7 44 24 ?? 90 01 00 00 -> patch the first byte of 90 01 00 00.

			constexpr uintptr_t TODO_RVA_Clear_MemsetFirstBlockSize = 0x5FD44;  // push 0x108 for FastMemset(this + 0x08, 0, 0x108).
			constexpr uintptr_t TODO_RVA_Clear_SelectedBlockOffset = 0x5FD5D;    // lea eax, [esi+0x118] before FastMemset(..., 0x50).
			constexpr uintptr_t TODO_RVA_Clear_FirstFlagsOword0 = 0x5FD64;       // movups [esi+0x168], xmm0.
			constexpr uintptr_t TODO_RVA_Clear_SelectedBlockSize = 0x5FD69;      // push 0x50 for FastMemset(this + 0x118, 0, 0x50).
			constexpr uintptr_t TODO_RVA_Clear_FirstFlagsOword1 = 0x5FD6D;       // movups [esi+0x178], xmm0.
			constexpr uintptr_t TODO_RVA_Clear_FirstFlagsLastByte = 0x5FD75;     // mov byte ptr [esi+0x188], 0.
			constexpr uintptr_t TODO_RVA_Clear_PlayerMaskByte = 0x5FD7D;         // mov byte ptr [esi+0x110], 0.
			constexpr uintptr_t TODO_RVA_Clear_MemsetSecondBlockSize = 0x5FD88; // push 0x108 for FastMemset(this + 0x198, 0, 0x108).
			constexpr uintptr_t TODO_RVA_Clear_SecondBlockOffset = 0x5FD91;      // lea eax, [esi+0x198].
			constexpr uintptr_t TODO_RVA_Clear_SelectedFlagsQword = 0x5FD9B;     // movq [esi+0x189], xmm0.
			constexpr uintptr_t TODO_RVA_Clear_SelectedFlagsWord = 0x5FDA3;      // mov word ptr [esi+0x191], 0.
			constexpr uintptr_t TODO_RVA_Clear_FinalBlockOffset = 0x5FDB3;       // lea eax, [esi+0x2A0].
			constexpr uintptr_t TODO_RVA_Clear_FinalBlockSize = 0x5FDB8;         // push 0x140 for FastMemset(this + 0x2A0, 0, 0x140).
			constexpr uintptr_t TODO_RVA_Clear_CopyTailDst0 = 0x5FDBF;           // movups [esi+0x3E0], xmm0.
			constexpr uintptr_t TODO_RVA_Clear_CopyTailDst1 = 0x5FDC8;           // movups [esi+0x3F0], xmm0.
			constexpr uintptr_t TODO_RVA_Clear_CopyTailDstByte = 0x5FDCF;        // mov byte ptr [esi+0x400], 0.
			constexpr uintptr_t TODO_RVA_Clear_SecondTailOffset = 0x5FDE2;       // movups [esi+0x401], xmm0.
			constexpr uintptr_t TODO_RVA_Clear_SecondTailOffsetPlus10 = 0x5FDEB; // movups [esi+0x411], xmm0.
			constexpr uintptr_t TODO_RVA_Clear_SecondTailOffsetPlus20 = 0x5FDF3; // movq [esi+0x421], xmm0.

			constexpr uintptr_t TODO_RVA_CopyVanilla_FirstCopySizeDwords = 0x5FE15; // CopyVanillaBlocks: mov ecx, 0x42 before rep movsd.
			constexpr uintptr_t TODO_RVA_CopyVanilla_FirstCopyDstOffset = 0x5FE21; // CopyVanillaBlocks: lea edi, [edx+0x198].
			constexpr uintptr_t TODO_RVA_CopyVanilla_TailSrc0 = 0x5FE2A;             // CopyVanillaBlocks: movups xmm0, [ebx+0x160].
			constexpr uintptr_t TODO_RVA_CopyVanilla_TailDst0 = 0x5FE33;             // CopyVanillaBlocks: movups [edx+0x3E0], xmm0.
			constexpr uintptr_t TODO_RVA_CopyVanilla_TailSrc1 = 0x5FE3A;             // CopyVanillaBlocks: movups xmm0, [ebx+0x170].
			constexpr uintptr_t TODO_RVA_CopyVanilla_TailDst1 = 0x5FE41;             // CopyVanillaBlocks: movups [edx+0x3F0], xmm0.
			constexpr uintptr_t TODO_RVA_CopyVanilla_TailSrcByte = 0x5FE47;          // CopyVanillaBlocks: mov al, [ebx+0x180].
			constexpr uintptr_t TODO_RVA_CopyVanilla_TailDstByte = 0x5FE4D;          // CopyVanillaBlocks: mov [edx+0x400], al.
			constexpr uintptr_t TODO_RVA_CopyVanilla_FinalCopySizeDwords = 0x5FE5C;  // CopyVanillaBlocks: mov ecx, 0x64 before final rep movsd.

			constexpr uintptr_t TODO_RVA_Merge_DestFreshBlock = 0x5FFDD;        // MergeCharacterSyncData: lea edx, [ecx+0x198].
			constexpr uintptr_t TODO_RVA_Merge_IncomingFlagOffset = 0x5FFFE;    // MergeCharacterSyncData: mov cl, [eax+esi+0x160].
			constexpr uintptr_t TODO_RVA_Merge_DestFlagOffset = 0x6000D;        // MergeCharacterSyncData: mov [eax+ebx+0x3E0], cl.
			constexpr uintptr_t TODO_RVA_Merge_LoopLimit = 0x6001C;             // MergeCharacterSyncData: cmp eax, 0x21.
			constexpr uintptr_t TODO_RVA_Merge_FinalCopySizeDwords = 0x6002F;    // MergeCharacterSyncData: mov ecx, 0x64 before final rep movsd.

			constexpr uintptr_t TODO_RVA_Apply_PacketEntryBase = 0x8779B;     // ApplyCharacterSyncPacket: lea edi, [ebx+0x1A0].
			constexpr uintptr_t TODO_RVA_Apply_WorkshopSplit = 0x877B2;     // ApplyCharacterSyncPacket: cmp edx, 0x21.
			constexpr uintptr_t TODO_RVA_Apply_SyncFlagOffset = 0x877CE;     // ApplyCharacterSyncPacket: mov bl, [edx+ebx+0x3E0].
			constexpr uintptr_t TODO_RVA_Apply_LoopLimitTotal = 0x87A1B;    // ApplyCharacterSyncPacket: cmp edx, 0x49.

			constexpr uintptr_t TODO_RVA_Build_FirstPacketEntryBase = 0x88766;       // BuildCharacterSyncPacket: add ebx, 0x1A0.
			constexpr uintptr_t TODO_RVA_Build_FirstLoopFlagOffset = 0x88965;        // BuildCharacterSyncPacket: mov [packet+edi+0x168], al.
			constexpr uintptr_t TODO_RVA_Build_SplitLoopLimit = 0x88A85;             // BuildCharacterSyncPacket: cmp edi, 0x21.
			constexpr uintptr_t TODO_RVA_Build_WorkshopTableByteOffset = 0x88A94;    // BuildCharacterSyncPacket: lea eax, [esi+0x122C] (1163*4).
			constexpr uintptr_t TODO_RVA_Build_WorkshopDestOffset = 0x88AA8;         // BuildCharacterSyncPacket: lea edx, [ebx+0x2A0].
			constexpr uintptr_t TODO_RVA_Build_WorkshopSyncFlagOffset = 0x88A60;     // BuildCharacterSyncPacket: mov [packet+edi+0x3E0], al.
			constexpr uintptr_t TODO_RVA_Build_WorkshopTailFlagOffset = 0x88C76;     // BuildCharacterSyncPacket: mov [packet+ecx+0x401], al.
			constexpr uintptr_t TODO_RVA_Build_Selected10ByteOffset = 0x88D02;       // BuildCharacterSyncPacket: lea eax, [esi+0x1468] (1306*4).
			constexpr uintptr_t TODO_RVA_Build_SelectedPacketBlockOffset = 0x88D28;  // BuildCharacterSyncPacket: add ebx, 0x118.
			constexpr uintptr_t TODO_RVA_Build_SelectedFlagWriteOffset = 0x8901A;    // BuildCharacterSyncPacket: mov [packet+edx+0x189], al.
			constexpr uintptr_t TODO_RVA_Build_SelectedFlagCheckOffset = 0x890D0;    // BuildCharacterSyncPacket: cmp byte ptr [packet+edi+0x189], 0.
			constexpr uintptr_t TODO_RVA_Build_SelectedFlagFallbackOffset = 0x890FD; // BuildCharacterSyncPacket: mov [packet+edi+0x189], al.
			constexpr uintptr_t TODO_RVA_Build_WorkshopTableIndex = 0x8905E;         // BuildCharacterSyncPacket: mov ebx, 0x48B (1163).

			bool ok = true;
			ok = PatchU32(TODO_RVA_Clear_MemsetFirstBlockSize, firstPacketBlockBytes, "ClearCharacterSyncPacket first block memset size") && ok;
			ok = PatchU32(TODO_RVA_Clear_SelectedBlockOffset, workshopIdsOffset + 8, "ClearCharacterSyncPacket selected block offset") && ok;
			ok = PatchU32(TODO_RVA_Clear_FirstFlagsOword0, packetFlagOffset + 8, "ClearCharacterSyncPacket first flags oword0") && ok;
			ok = PatchU8(TODO_RVA_Clear_SelectedBlockSize, 0x50, "ClearCharacterSyncPacket selected block memset size") && ok;
			ok = PatchU32(TODO_RVA_Clear_FirstFlagsOword1, packetFlagOffset + 0x18, "ClearCharacterSyncPacket first flags oword1") && ok;
			ok = PatchU32(TODO_RVA_Clear_FirstFlagsLastByte, workshopFlagsOffset, "ClearCharacterSyncPacket first flags last byte") && ok;
			ok = PatchU32(TODO_RVA_Clear_PlayerMaskByte, workshopIdsOffset, "ClearCharacterSyncPacket player mask byte") && ok;
			ok = PatchU32(TODO_RVA_Clear_MemsetSecondBlockSize, firstPacketBlockBytes, "ClearCharacterSyncPacket second block memset size") && ok;
			ok = PatchU32(TODO_RVA_Clear_SecondBlockOffset, secondPacketOffset, "ClearCharacterSyncPacket second block offset") && ok;
			ok = PatchU32(TODO_RVA_Clear_SelectedFlagsQword, workshopFlagsOffset + 1, "ClearCharacterSyncPacket selected flags qword") && ok;
			ok = PatchU32(TODO_RVA_Clear_SelectedFlagsWord, workshopFlagsOffset + 9, "ClearCharacterSyncPacket selected flags word") && ok;
			ok = PatchU32(TODO_RVA_Clear_FinalBlockOffset, finalPacketOffset, "ClearCharacterSyncPacket final block offset") && ok;
			ok = PatchU32(TODO_RVA_Clear_FinalBlockSize, 0x140, "ClearCharacterSyncPacket final block size") && ok;
			ok = PatchU32(TODO_RVA_Clear_CopyTailDst0, copyTailDst0, "ClearCharacterSyncPacket tail dst0") && ok;
			ok = PatchU32(TODO_RVA_Clear_CopyTailDst1, copyTailDst1, "ClearCharacterSyncPacket tail dst1") && ok;
			ok = PatchU32(TODO_RVA_Clear_CopyTailDstByte, copyTailDstByte, "ClearCharacterSyncPacket tail byte") && ok;
			ok = PatchU32(TODO_RVA_Clear_SecondTailOffset, secondPacketTailOffset, "ClearCharacterSyncPacket second tail offset") && ok;
			ok = PatchU32(TODO_RVA_Clear_SecondTailOffsetPlus10, secondPacketTailOffset + 0x10, "ClearCharacterSyncPacket second tail offset + 0x10") && ok;
			ok = PatchU32(TODO_RVA_Clear_SecondTailOffsetPlus20, secondPacketTailOffset + 0x20, "ClearCharacterSyncPacket second tail offset + 0x20") && ok;

			ok = PatchU32(TODO_RVA_CopyVanilla_FirstCopySizeDwords, firstPacketBlockBytes / 4, "CopyVanillaBlocks first copy dword count") && ok;
			ok = PatchU32(TODO_RVA_CopyVanilla_FirstCopyDstOffset, secondPacketOffset, "CopyVanillaBlocks first copy destination offset") && ok;
			ok = PatchU32(TODO_RVA_CopyVanilla_TailSrc0, copyTailSrc0, "CopyVanillaBlocks tail src0") && ok;
			ok = PatchU32(TODO_RVA_CopyVanilla_TailDst0, copyTailDst0, "CopyVanillaBlocks tail dst0") && ok;
			ok = PatchU32(TODO_RVA_CopyVanilla_TailSrc1, copyTailSrc1, "CopyVanillaBlocks tail src1") && ok;
			ok = PatchU32(TODO_RVA_CopyVanilla_TailDst1, copyTailDst1, "CopyVanillaBlocks tail dst1") && ok;
			ok = PatchU32(TODO_RVA_CopyVanilla_TailSrcByte, copyTailSrcByte, "CopyVanillaBlocks tail src byte") && ok;
			ok = PatchU32(TODO_RVA_CopyVanilla_TailDstByte, copyTailDstByte, "CopyVanillaBlocks tail dst byte") && ok;
			ok = PatchU32(TODO_RVA_CopyVanilla_FinalCopySizeDwords, (0x190 + (N * 8)) / 4, "CopyVanillaBlocks final copy dword count") && ok;

			ok = PatchU8(TODO_RVA_Merge_LoopLimit, static_cast<uint8_t>(split), "MergeCharacterSyncData loop limit") && ok;
			ok = PatchU32(TODO_RVA_Merge_DestFreshBlock, secondPacketOffset, "MergeCharacterSyncData dest fresh block") && ok;
			ok = PatchU32(TODO_RVA_Merge_IncomingFlagOffset, packetFlagOffset, "MergeCharacterSyncData incoming flag offset") && ok;
			ok = PatchU32(TODO_RVA_Merge_DestFlagOffset, syncObjFlagOffset, "MergeCharacterSyncData dest flag offset") && ok;
			ok = PatchU32(TODO_RVA_Merge_FinalCopySizeDwords, (0x190 + (N * 8)) / 4, "MergeCharacterSyncData final copy dword count") && ok;

			ok = PatchU32(TODO_RVA_Apply_PacketEntryBase, secondPacketOffset + 8, "ApplyCharacterSyncPacket packet entry base") && ok;
			ok = PatchU8(TODO_RVA_Apply_WorkshopSplit, static_cast<uint8_t>(split), "ApplyCharacterSyncPacket workshop split") && ok;
			ok = PatchU32(TODO_RVA_Apply_SyncFlagOffset, syncObjFlagOffset, "ApplyCharacterSyncPacket sync flag offset") && ok;
			ok = PatchU8(TODO_RVA_Apply_LoopLimitTotal, static_cast<uint8_t>(total), "ApplyCharacterSyncPacket total loop limit") && ok;

			ok = PatchU32(TODO_RVA_Build_FirstPacketEntryBase, secondPacketOffset + 8, "BuildCharacterSyncPacket first packet entry base") && ok;
			ok = PatchU32(TODO_RVA_Build_FirstLoopFlagOffset, packetFlagOffset + 8, "BuildCharacterSyncPacket first loop flag offset") && ok;
			ok = PatchU8(TODO_RVA_Build_SplitLoopLimit, static_cast<uint8_t>(split), "BuildCharacterSyncPacket first loop split") && ok;
			ok = PatchU32(TODO_RVA_Build_WorkshopTableByteOffset, workshopSkinEntryByteOffset, "BuildCharacterSyncPacket workshop table byte offset") && ok;
			ok = PatchU32(TODO_RVA_Build_WorkshopDestOffset, finalPacketOffset, "BuildCharacterSyncPacket workshop dest offset") && ok;
			ok = PatchU32(TODO_RVA_Build_WorkshopSyncFlagOffset, syncObjFlagOffset, "BuildCharacterSyncPacket workshop sync flag offset") && ok;
			ok = PatchU32(TODO_RVA_Build_WorkshopTailFlagOffset, secondPacketTailOffset, "BuildCharacterSyncPacket workshop tail flag offset") && ok;
			ok = PatchU32(TODO_RVA_Build_Selected10ByteOffset, selected10ByteOffset, "BuildCharacterSyncPacket selected10 byte offset") && ok;
			ok = PatchU32(TODO_RVA_Build_SelectedPacketBlockOffset, workshopIdsOffset + 8, "BuildCharacterSyncPacket selected packet block offset") && ok;
			ok = PatchU32(TODO_RVA_Build_SelectedFlagWriteOffset, workshopFlagsOffset + 1, "BuildCharacterSyncPacket selected flag write offset") && ok;
			ok = PatchU32(TODO_RVA_Build_SelectedFlagCheckOffset, workshopFlagsOffset + 1, "BuildCharacterSyncPacket selected flag check offset") && ok;
			ok = PatchU32(TODO_RVA_Build_SelectedFlagFallbackOffset, workshopFlagsOffset + 1, "BuildCharacterSyncPacket selected flag fallback offset") && ok;
			ok = PatchU32(TODO_RVA_Build_WorkshopTableIndex, workshopSkinEntryIndex, "BuildCharacterSyncPacket workshop table index") && ok;

			Util::Logger::Instance().Get()->info(
				"[NetworkPatch] Expanded packet patches applied success={} addon_count={} split={} total={} first_block=0x{:X} second_block=0x{:X} final_block=0x{:X}.",
				ok,
				N,
				split,
				total,
				firstPacketBlockBytes,
				secondPacketOffset,
				finalPacketOffset);
			return ok;
		}
	}
	void RegisterSaveName(const std::string& name) {
		g_saveName = SanitizeSaveName(name);
		RefreshSaveFileNames();
		Util::Logger::Instance().Get()->info("[Save] Registered mod save name '{}' -> '{}' / '{}'.", g_saveName, g_saveFileName, g_saveBackupFileName);
	}

	const std::string& GetSaveFileName() {
		return g_saveFileName;
	}

	const std::string& GetSaveBackupFileName() {
		return g_saveBackupFileName;
	}

	bool ApplySaveFileNamePatch() {
		RefreshSaveFileNames();
		const bool mainPatched = PatchPointerReferences(
			{ 0xE8E8A, 0xE8EA6, 0xE8ED9, 0xE9096, 0xE90C7, 0xE912B, 0xE9344 },
			{ 0x1C46EC },
			g_saveFileName.c_str());
		const bool bakPatched = PatchPointerReferences(
			{ 0xE915F, 0xE9176 },
			{ 0x1C46F0 },
			g_saveBackupFileName.c_str());
		Util::Logger::Instance().Get()->info(
			"[Save] Save filename patch applied main='{}' bak='{}' success={}.",
			g_saveFileName,
			g_saveBackupFileName,
			mainPatched && bakPatched);
		return mainPatched && bakPatched;
	}

	bool ApplySaveExpansionPatches() {
		const bool saveNamePatched = ApplySaveFileNamePatch();
		if (!saveNamePatched) {
			return false;
		}
		int N = CharacterConfig::Instance().GetAddonCount();
		Util::Logger::Instance().Get()->info("[Save] Applying save expansion patches. phase={} addon_count={}.", kSavePatchPhase, N);
		if (N <= 0) {
			Util::Logger::Instance().Get()->info("[Save] No addon characters registered; save/lobby expansion patches are skipped and vanilla character/workshop behavior is preserved.");
			return saveNamePatched;
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
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xFD2E6, { // updated
			(uint8_t)(newSize),
			(uint8_t)(newSize >> 8),
			(uint8_t)(newSize >> 16),
			(uint8_t)(newSize >> 24)
			});
		// FastMemset(v3, 0, 2128) -> newSize
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xFD310, { // updated
			(uint8_t)(newSize),
			(uint8_t)(newSize >> 8),
			(uint8_t)(newSize >> 16),
			(uint8_t)(newSize >> 24)
			});
		// this[2] = 2124 -> newCapacity
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xFD31A, { // updated
			(uint8_t)(newCapacity),
			(uint8_t)(newCapacity >> 8),
			(uint8_t)(newCapacity >> 16),
			(uint8_t)(newCapacity >> 24)
			});

		// ============================================================
		//  2. SAVE FILE - VALIDATION BYPASSES
		// ============================================================

		// ValidateFileSaveSize -> always return 1
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x130F10, { 0xB0, 0x01, 0xC3, 0x90, 0x90, 0x90 }); // updated

		// sub_8C7020 (backup gen) - skip size check 2128/1600/1552
		HookCrashers::Util::MemoryPatcher::PatchBytes(0xE90D3, { 0xEB, 0x2B, 0x90, 0x90, 0x90, 0x90 }); // updated

		// sub_90EDD0 - cmp edx, 2128
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x130F68, { // updated
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
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12F4A7, { // updated
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12F4B0, { // updated
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});

		// GenerateDefaultKeybinds
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12EE3D, { // updated
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12EE46, { // updated
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});

		// sub_91B020 - ACCEPT keybind
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x13D200 + 3, { // => 0x13D200 (+0x21B0) updated
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x13D209 + 3, { // updated
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});

		// sub_91B4E0 - WRITE keybind (exit settings)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x13D6EB + 3, { // updated
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x13D6F4 + 3, { // updated
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});

		// sub_91BFA0 - REVERT keybind / open settings display
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x13E182 + 3, { // updated
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x13E18B + 3, { // updated
			(uint8_t)(newKeybindsLimit),
			(uint8_t)(newKeybindsLimit >> 8),
			(uint8_t)(newKeybindsLimit >> 16),
			(uint8_t)(newKeybindsLimit >> 24)
			});

		// sub_90CC40 - CheckKeybindsZero (calls GenerateDefaultKeybinds if all zero)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12EDB6, { // updated
			(uint8_t)(0x832 + addonBytes),
			(uint8_t)((0x832 + addonBytes) >> 8),
			(uint8_t)((0x832 + addonBytes) >> 16),
			(uint8_t)((0x832 + addonBytes) >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12EDBF, { // updated
			(uint8_t)(0x832 + addonBytes),
			(uint8_t)((0x832 + addonBytes) >> 8),
			(uint8_t)((0x832 + addonBytes) >> 16),
			(uint8_t)((0x832 + addonBytes) >> 24)
			});

		// ============================================================
		//  4. CHARACTER DATA - DEFAULT CHARACTERS
		//     CreateDefaultCharacters loop limit (0x2A = 42 chars)
		// ============================================================

		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12F1BE, { (uint8_t)(TOTAL_ORIGINAL_CHARS + N) }); // updated

		// sub_90C9C0 - ValidateCharacterStats loop limit (i < 0x821 -> i < 0x821 + addonBytes)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x12ED84, { // updated
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
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x830A0, { (uint8_t)total }); // updated
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x830A4, { (uint8_t)total }); // updated
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x830E0, { (uint8_t)(tableBytes & 0xFF), (uint8_t)(tableBytes >> 8) }); // updated

		// d_characterTable malloc size
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x10C805, { // updated
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
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x84701, { (uint8_t)(tableBytes & 0xFF), (uint8_t)(tableBytes >> 8) }); // updated
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x847BD, { (uint8_t)split }); // updated
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x84A2C, { (uint8_t)total }); // updated
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x847D2, { (uint8_t)(-totalBase) }); // updated
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
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8E9A0 + 2, { (uint8_t)(total) }); // updated
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8E9C2 + 2, { (uint8_t)(split) }); // updated

		// ValidateAndRevokeDLC
		if (kPhase4EnableValidateAndRevokeDLC) {
			if (kPhase4ExpandValidateAndRevokeDLCScan) {
				HookCrashers::Util::MemoryPatcher::PatchBytes(0x873F1, { (uint8_t)total }); // updated
			}
			if (kPhase4SkipValidateAndRevokeDLCRebuild) {
				HookCrashers::Util::MemoryPatcher::PatchBytes(0x872CB, { 0x90, 0x90, 0x90, 0x90, 0x90 }); // updated
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
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x1055CA + 2, { (uint8_t)(totalBase - 1) });               // Default 31 (0x1F)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x1055F3 + 2, { (uint8_t)(-totalBase) });                  // Default -32 (0xE0)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x105629 + 2, { (uint8_t)(totalBase + 1) });               // Default 33 (0x21)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x105637 + 2, { (uint8_t)(totalBase + 10 + 1) }); // Default 43 (0x2B)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x105645 + 2, { (uint8_t)(totalBase + 20 + 1) }); // Default 53 (0x35)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x105653 + 2, { (uint8_t)(totalBase + 30 + 1) }); // Default 63 (0x3F)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x10563C + 2, { (uint8_t)(totalBase + 20 + 1) }); // Default 53 (0x35)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x10564A + 2, { (uint8_t)(totalBase + 30 + 1) }); // Default 63 (0x3F)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x10565C + 2, { (uint8_t)(total) });              // Default 73 (0x49)
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x10562E + 2, { (uint8_t)(totalBase + 10 + 1) }); // Default 43 (0x2B)
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
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x14CF2E + 2, { (uint8_t)(workshopSelectionStart) }); // updated // cmp eax, 0x20
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x14CF4B + 2, { (uint8_t)(workshopSelectionStart) }); // updated // sub eax, 0x20
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
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8D625, { (uint8_t)(totalBase + 1) }); // updated
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8D5F4, { (uint8_t)(total) }); // updated
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8D5FB, { (uint8_t)(total) }); // updated

		// AttachWorkshopSkin
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8D23C, { (uint8_t)(totalBase + 1) }); // updated
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
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x88A94, { // updated
			(uint8_t)(workshopSkinEntryByteOffset),
			(uint8_t)(workshopSkinEntryByteOffset >> 8),
			(uint8_t)(workshopSkinEntryByteOffset >> 16),
			(uint8_t)(workshopSkinEntryByteOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x88D02, { // updated
			(uint8_t)(selected10ByteOffset),
			(uint8_t)(selected10ByteOffset >> 8),
			(uint8_t)(selected10ByteOffset >> 16),
			(uint8_t)(selected10ByteOffset >> 24)
			});
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8905E, { // updated
			(uint8_t)(workshopSkinEntryIndex),
			(uint8_t)(workshopSkinEntryIndex >> 8),
			(uint8_t)(workshopSkinEntryIndex >> 16),
			(uint8_t)(workshopSkinEntryIndex >> 24)
			});

		/*HookCrashers::Util::MemoryPatcher::PatchBytes(0x88D60 + 2, {
			(uint8_t)(selected10ByteOffset & 0xFF),
			(uint8_t)((selected10ByteOffset >> 8) & 0xFF),
			0x00, 0x00
			});*/
		ApplyNetworkPacketExpansionPatches(
			N,
			split,
			total,
			selected10ByteOffset,
			workshopSkinEntryIndex,
			workshopSkinEntryByteOffset,
			packetFlagOffset,
			syncObjFlagOffset,
			firstPacketBlockBytes,
			secondPacketOffset,
			workshopIdsOffset,
			workshopFlagsOffset,
			secondPacketTailOffset,
			finalPacketOffset,
			copyTailSrc0,
			copyTailDst0,
			copyTailSrc1,
			copyTailDst1,
			copyTailSrcByte,
			copyTailDstByte);

		// ResolveWorkshopCharacterConflicts
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x83FFD, { (uint8_t)(split) }); // updated
		
		// GetCurrentWorkshopSkinEntry
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8D23C, { (uint8_t)(split) }); // updated
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8D26C, { (uint8_t)(split) }); // updated

		// BuildCharacterSlotDisplayNameA
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8A5C9, { (uint8_t)(-split) }); // lea eax, [edi - workshop_start]
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8A5D5, { (uint8_t)(split - 1) }); // display add-on slot index from workshop_start
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8A5E4, { (uint8_t)(split - 2) }); // valid real character max = split - 1

		// BuildCharacterSlotDisplayNameW
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8A678, { (uint8_t)(-split) }); // lea eax, [ecx - workshop_start]
		const uint32_t workshopNameBase = 953 - split;
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8A68A, {
			(uint8_t)(workshopNameBase & 0xFF),
			(uint8_t)((workshopNameBase >> 8) & 0xFF),
			(uint8_t)((workshopNameBase >> 16) & 0xFF),
			(uint8_t)((workshopNameBase >> 24) & 0xFF)
		}); // add edx, 953 - workshop_start
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8A6BB, { (uint8_t)(split - 2) }); // valid real character max = split - 1

		// FormatCharacterSlotNameA / GetDefaultCharacterSlotNamePtrW
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8FC6C, { (uint8_t)(split - 1) }); // max default-name character id
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8FD12, { (uint8_t)(split - 1) }); // max default-name character id, wide path
		// TryAppendWorkshopCharacterNameA
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8A730, { (uint8_t)(total) }); // cmp slot_index, total

		// ClearInvalidReadyWorkshopSkinSelections
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8EB14, { (uint8_t)(total) }); // cmp slot_index, total

		// GetCharacterSlotDisplayFrame
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8EB56, { (uint8_t)(total) }); // cmp slot_index, total

		// CallSWFFunction case 232 fallback first workshop slot
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x106097, { (uint8_t)(split), 0x00, 0x00, 0x00 }); // mov eax, workshop_start

		// NormalizeCharacterIdToSkinSlotIndex
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8FBF1, { (uint8_t)(total) }); // cmp normalized_index, total
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x8FC05, { (uint8_t)(total) }); // cmp normalized_index, total

		// LobbyTrySelectChar clone menu range
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x105D76, { (uint8_t)(split) }); // cmp selected_char_id, workshop_start before opening clone menu

		// HandleCloneWorkshopSlotConfirm
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x16080A, { (uint8_t)(-split) }); // lea esi, [eax - workshop_start]

		// ClearReadyCustomCharacterSelections
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x87BEC, { (uint8_t)(split) }); // scan real/addon slots up to workshop_start
		const uint32_t shiftedWorkshopSkinEntryByteOffset = (1130 + split) * 4;
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x87D0E, {
			(uint8_t)(shiftedWorkshopSkinEntryByteOffset & 0xFF),
			(uint8_t)((shiftedWorkshopSkinEntryByteOffset >> 8) & 0xFF),
			(uint8_t)((shiftedWorkshopSkinEntryByteOffset >> 16) & 0xFF),
			(uint8_t)((shiftedWorkshopSkinEntryByteOffset >> 24) & 0xFF)
		}); // workshop skin table base = (1130 + workshop_start) * 4
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x87D6C, { (uint8_t)(split) }); // workshop char slot base
		return true;
	}
}

		// SetFlagIsCharWorkshop
		// HookCrashers::Util::MemoryPatcher::PatchBytes(0x36986 + 2, { (uint8_t)(totalBase + 1) }); // Default 33 (0x21)

		// GetCharacterGameCompletedForPlayer <- decompile
		//HookCrashers::Util::MemoryPatcher::PatchBytes(0x102B54 + 0x8E0 + 2, { (uint8_t)(-(totalBase + 10)) });
		//HookCrashers::Util::MemoryPatcher::PatchBytes(0x102B64 + 0x8E0 + 2, { (uint8_t)(totalBase) });
		//HookCrashers::Util::MemoryPatcher::PatchBytes(0x102B72 + 0x8E0 + 2, { (uint8_t)(totalBase + 10) });


#if 0
		// IsCharacterIndexValid
		/*HookCrashers::Util::MemoryPatcher::PatchBytes(0x8E726 + 2, {(uint8_t)(total)});

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
		int wsMaskOffset = 0x108 + (N * 8);
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
		HookCrashers::Util::MemoryPatcher::PatchBytes(0x877CE, { // updated
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
HookCrashers::Util::MemoryPatcher::PatchBytes(0x88D02, { // updated
	(uint8_t)(workshopSkinEntryByteOffset),
	(uint8_t)(workshopSkinEntryByteOffset >> 8),
	(uint8_t)(workshopSkinEntryByteOffset >> 16),
	(uint8_t)(workshopSkinEntryByteOffset >> 24)
	});
HookCrashers::Util::MemoryPatcher::PatchBytes(0x8905E, { // updated
	(uint8_t)(workshopSkinEntryIndex),
	(uint8_t)(workshopSkinEntryIndex >> 8),
	(uint8_t)(workshopSkinEntryIndex >> 16),
	(uint8_t)(workshopSkinEntryIndex >> 24)
	});
HookCrashers::Util::MemoryPatcher::PatchBytes(0x88C76, { // updated
	(uint8_t)(wsFlagsDest),
	(uint8_t)(wsFlagsDest >> 8),
	(uint8_t)(wsFlagsDest >> 16),
	(uint8_t)(wsFlagsDest >> 24)
	});

// BuildCharacterSyncPacket
HookCrashers::Util::MemoryPatcher::PatchBytes(0x88A85, { (uint8_t)(split) }); // updated
HookCrashers::Util::MemoryPatcher::PatchBytes(0x88A94, { // updated
	(uint8_t)(workshopByteOffset & 0xFF),
	(uint8_t)((workshopByteOffset >> 8) & 0xFF),
	0x00, 0x00
	});
#endif













