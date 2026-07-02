#include "SavePatches.h"
#include "CharacterConfig.h"
#include "../Util/MemoryPatcher.h"
#include "../Util/Logger.h"
#include "../../include/HookCrashers/Public/Globals.h"
#include <string>
#include <vector>

namespace HookCrashers::Save {
namespace {
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

	std::vector<uint8_t> U16(uint32_t value) {
		return {
			static_cast<uint8_t>(value & 0xFF),
			static_cast<uint8_t>((value >> 8) & 0xFF)
		};
	}

	std::vector<uint8_t> U32(uint32_t value) {
		return {
			static_cast<uint8_t>(value & 0xFF),
			static_cast<uint8_t>((value >> 8) & 0xFF),
			static_cast<uint8_t>((value >> 16) & 0xFF),
			static_cast<uint8_t>((value >> 24) & 0xFF)
		};
	}

	bool PatchBytes(uintptr_t rva, const std::vector<uint8_t>& bytes) {
		return Util::MemoryPatcher::PatchBytes(rva, bytes);
	}

	bool PatchU8(uintptr_t rva, int value) {
		return PatchBytes(rva, { static_cast<uint8_t>(value) });
	}

	bool PatchU16(uintptr_t rva, int value) {
		return PatchBytes(rva, U16(static_cast<uint32_t>(value)));
	}

	bool PatchU32(uintptr_t rva, int value) {
		return PatchBytes(rva, U32(static_cast<uint32_t>(value)));
	}

	bool PatchPointerReferences(const std::vector<uintptr_t>& operandRvas, const std::vector<uintptr_t>& pointerRvas, const char* replacement) {
		bool success = true;
		const std::vector<uint8_t> bytes = PointerBytes(replacement);
		for (uintptr_t rva : operandRvas) {
			success = PatchBytes(rva + 1, bytes) && success;
		}
		for (uintptr_t rva : pointerRvas) {
			success = PatchBytes(rva, bytes) && success;
		}
		return success;
	}
}

void RegisterSaveName(const std::string& name) {
	g_saveName = SanitizeSaveName(name);
	RefreshSaveFileNames();
	Util::Logger::Instance().Get()->debug("[Save] Registered mod save name '{}' -> '{}' / '{}'.", g_saveName, g_saveFileName, g_saveBackupFileName);
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
	Util::Logger::Instance().Get()->debug(
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

	const int addonCount = CharacterConfig::Instance().GetAddonCount();
	if (addonCount <= 0) {
		Util::Logger::Instance().Get()->debug("[Save] No addon characters registered; save/lobby expansion patches skipped.");
		return true;
	}

	const int totalSlots = TOTAL_ORIGINAL_SLOTS + addonCount;
	const int workshopStart = BASE_FRESH_COUNT + 1 + addonCount;
	const int tableBytes = totalSlots * 4;
	const int saveSize = BASE_SAVE_SIZE + (CHAR_DATA_SIZE * addonCount);
	const int saveCapacity = BASE_SAVE_CAPACITY + (CHAR_DATA_SIZE * addonCount);
	const int addonBytes = CHAR_DATA_SIZE * addonCount;
	const int baseCharacterCount = BASE_CHAR_COUNT + addonCount;
	int classicAddonCount = 0;
	for (const auto& addon : CharacterConfig::Instance().GetAddons()) {
		if (!addon.freshOnly) {
			++classicAddonCount;
		}
	}
	const int statsClassicCount = BASE_CLASSIC_COUNT + classicAddonCount;
	const int statsFreshCount = BASE_FRESH_COUNT + addonCount;
	const int statsTotalCount = statsFreshCount + WORKSHOP_CHAR_COUNT;
	const int keybindsOffset = BASE_KEYBINDS_OFFSET + addonBytes;
	const int characterTableBytes = CHAR_TABLE_BASE_SIZE + (CHAR_TABLE_ENTRY_SIZE * addonCount);
	const int workshopSlotTableOffset = (1131 + baseCharacterCount) * 4;
	const int workshopSkinEntryOffset = (1130 + workshopStart) * 4;
	const int workshopSelectionStart = baseCharacterCount;
	const int workshopNameBase = 953 - workshopStart;

	Util::Logger::Instance().Get()->info(
		"[Save] Applying expansion patches addons={} workshop_start={} total_slots={} save_size={} char_table_size={}.",
		addonCount,
		workshopStart,
		totalSlots,
		saveSize,
		characterTableBytes);

	bool success = true;

	// Save buffer allocation and validation.
	success = PatchU32(0xFD2E6, saveSize) && success;
	success = PatchU32(0xFD310, saveSize) && success;
	success = PatchU32(0xFD31A, saveCapacity) && success;
	success = PatchBytes(0x130F10, { 0xB0, 0x01, 0xC3, 0x90, 0x90, 0x90 }) && success;
	success = PatchBytes(0xE90D3, { 0xEB, 0x2B, 0x90, 0x90, 0x90, 0x90 }) && success;
	success = PatchU32(0x130F68, saveSize) && success;

	// Keybind data starts after the expanded character block.
	success = PatchU32(0x12F4A7, keybindsOffset) && success;
	success = PatchU32(0x12F4B0, keybindsOffset) && success;
	success = PatchU32(0x12EE3D, keybindsOffset) && success;
	success = PatchU32(0x12EE46, keybindsOffset) && success;
	success = PatchU32(0x13D200 + 3, keybindsOffset) && success;
	success = PatchU32(0x13D209 + 3, keybindsOffset) && success;
	success = PatchU32(0x13D6EB + 3, keybindsOffset) && success;
	success = PatchU32(0x13D6F4 + 3, keybindsOffset) && success;
	success = PatchU32(0x13E182 + 3, keybindsOffset) && success;
	success = PatchU32(0x13E18B + 3, keybindsOffset) && success;
	success = PatchU32(0x12EDB6, 0x832 + addonBytes) && success;
	success = PatchU32(0x12EDBF, 0x832 + addonBytes) && success;

	// Save character data loops.
	success = PatchU8(0x12F1BE, TOTAL_ORIGINAL_CHARS + addonCount) && success;
	success = PatchU32(0x12ED84, 0x821 + addonBytes) && success;

	// LobbyManager slot table and character table allocation.
	success = PatchU8(0x830A0, totalSlots) && success;
	success = PatchU8(0x830A4, totalSlots) && success;
	success = PatchU16(0x830E0, tableBytes) && success;
	success = PatchU32(0x10C805, characterTableBytes) && success;

	// RebuildCharacterSlotTable.
	success = PatchU16(0x84701, tableBytes) && success;
	success = PatchU8(0x847BD, workshopStart) && success;
	success = PatchU8(0x84A2C, totalSlots) && success;
	success = PatchU8(0x847D2, -baseCharacterCount) && success;

	// Character unlock and DLC validation.
	success = PatchU8(0x8E9A0 + 2, totalSlots) && success;
	success = PatchU8(0x8E9C2 + 2, workshopStart) && success;
	success = PatchU8(0x873F1, totalSlots) && success;
	success = PatchU8(0x1055CA + 2, baseCharacterCount - 1) && success;
	success = PatchU8(0x1055F3 + 2, -baseCharacterCount) && success;
	success = PatchU8(0x105629 + 2, baseCharacterCount + 1) && success;
	success = PatchU8(0x105637 + 2, baseCharacterCount + 11) && success;
	success = PatchU8(0x10562E + 2, baseCharacterCount + 11) && success;
	success = PatchU8(0x105645 + 2, baseCharacterCount + 21) && success;
	success = PatchU8(0x10563C + 2, baseCharacterCount + 21) && success;
	success = PatchU8(0x105653 + 2, baseCharacterCount + 31) && success;
	success = PatchU8(0x10564A + 2, baseCharacterCount + 31) && success;
	success = PatchU8(0x10565C + 2, totalSlots) && success;

	// Selection and skin attachment boundaries.
	success = PatchU8(0x14CF2E + 2, workshopSelectionStart) && success;
	success = PatchU8(0x14CF4B + 2, workshopSelectionStart) && success;
	success = PatchU8(0x8D625, baseCharacterCount + 1) && success;
	success = PatchU8(0x8D5F4, totalSlots) && success;
	success = PatchU8(0x8D5FB, totalSlots) && success;
	success = PatchU8(0x8D23C, workshopStart) && success;
	success = PatchU8(0x8D26C, workshopStart) && success;

	// Add-on display names and default localized names.
	success = PatchU8(0x8A5C9, -workshopStart) && success;
	success = PatchU8(0x8A5D5, workshopStart - 1) && success;
	success = PatchU8(0x8A5E4, workshopStart - 2) && success;
	success = PatchU8(0x8A678, -workshopStart) && success;
	success = PatchU32(0x8A68A, workshopNameBase) && success;
	success = PatchU8(0x8A6BB, workshopStart - 2) && success;
	success = PatchU8(0x8FC6C, workshopStart - 1) && success;
	success = PatchU8(0x8FD12, workshopStart - 1) && success;
	success = PatchU8(0x8A730, totalSlots) && success;

	// Lobby workshop/addon behavior.
	success = PatchU8(0x83FFD, workshopStart) && success;
	success = PatchU8(0x8EB14, totalSlots) && success;
	success = PatchU8(0x8EB56, totalSlots) && success;
	success = PatchU32(0x106097, workshopStart) && success;
	success = PatchU8(0x105EAA, workshopStart) && success;
	success = PatchU8(0x8FBF1, totalSlots) && success;
	success = PatchU8(0x8FC05, totalSlots) && success;
	success = PatchU8(0x105D76, workshopStart) && success;
	success = PatchU8(0x16080A, -workshopStart) && success;



	// Stats menu character iterator. Classic addon frames use the empty 32..100 range;
	// fresh addon frames are inserted before the workshop block, so workshop starts after addons.
	success = PatchU32(0x14CEC4, statsTotalCount) && success; // reverse wrap: mov eax, total_count
	success = PatchU8(0x14CF1F, statsTotalCount) && success;  // forward wrap: cmp [index], total_count
	success = PatchU8(0x14CED6, statsClassicCount) && success; // classic valid range: index < classic_count
	success = PatchU8(0x14CF04, statsClassicCount) && success; // first fresh-only index check
	success = PatchU8(0x14CF30, statsFreshCount) && success;   // fresh/addon valid range before workshop
	success = PatchU8(0x14CF4D, statsFreshCount) && success;   // workshop index = index - fresh_count

	// Ready-state workshop validation.
	success = PatchU8(0x87BEC, workshopStart) && success;
	success = PatchU32(0x87D0E, workshopSkinEntryOffset) && success;
	success = PatchU8(0x87D6C, workshopStart) && success;
	success = PatchU32(0x87E26, workshopSlotTableOffset) && success;
	success = PatchU32(0x87E2B, workshopStart) && success;
	success = PatchU8(0x87F58, workshopStart) && success;
	success = PatchU8(0x87FD7, workshopStart) && success;
	success = PatchU8(0x88020, totalSlots) && success;

	// Network structural slot limits only. Packet buffer/copy/clear rewrites stay disabled.
	success = PatchU8(0x88A85, workshopStart) && success; // BuildCharacterSyncPacket: cmp edi, workshop_start
	success = PatchU8(0x877B2, workshopStart) && success; // ApplyCharacterSyncPacket: cmp edx, workshop_start
	success = PatchU8(0x87A1B, totalSlots) && success;    // ApplyCharacterSyncPacket: cmp edx, total_slots
	Util::Logger::Instance().Get()->info(
		"[NetworkPatch] Applied structural limits only split={} total={}; packet buffer expansion is disabled.",
		workshopStart,
		totalSlots);

	return success;
}
}


