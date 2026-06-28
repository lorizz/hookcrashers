#include "SyncCharacterListFromSaveHook.h"

#include "CharacterConfig.h"
#include "../Util/Logger.h"
#include "../../include/HookCrashers/Public/Globals.h"

#include <windows.h>
#include <detours.h>
#include <cstdint>
#include <sstream>

namespace HookCrashers::Save {
	namespace {
		constexpr uintptr_t kSyncCharacterListFromSaveRva = 0x85B20; // updated
		constexpr int kCharacterArrayEntrySize = 0x1090;
		constexpr int kSkinEntryByIndexOffsetDwords = 1130;

		using SyncCharacterListFromSaveFn = char(__thiscall*)(uint8_t* lobbyManager, uint32_t* saveVector, char mode);
		SyncCharacterListFromSaveFn g_originalSyncCharacterListFromSave = nullptr;

		int VectorCount(uint32_t* vector) {
			if (!vector || vector[1] < vector[0]) {
				return 0;
			}
			return static_cast<int>((vector[1] - vector[0]) / 32);
		}

		int CharacterArrayCount(uint8_t* lobbyManager) {
			if (!lobbyManager) {
				return 0;
			}
			const uint32_t begin = *reinterpret_cast<uint32_t*>(lobbyManager + 0x38);
			const uint32_t current = *reinterpret_cast<uint32_t*>(lobbyManager + 0x3C);
			if (!begin || current < begin) {
				return 0;
			}
			return static_cast<int>((current - begin) / kCharacterArrayEntrySize);
		}

		std::string DescribeSaveVector(uint32_t* vector) {
			std::ostringstream stream;
			if (!vector || !vector[0] || vector[1] < vector[0]) {
				return "<empty>";
			}

			const int count = VectorCount(vector);
			const int limit = count < 12 ? count : 12;
			for (int i = 0; i < limit; ++i) {
				uint8_t* entry = reinterpret_cast<uint8_t*>(vector[0] + i * 32);
				const uint32_t type = *reinterpret_cast<uint32_t*>(entry + 20);
				const uint32_t lo = *reinterpret_cast<uint32_t*>(entry + 24);
				const uint32_t hi = *reinterpret_cast<uint32_t*>(entry + 28);
				stream << "[i=" << i
					<< " type=0x" << std::hex << type
					<< " lo=0x" << lo
					<< " hi=0x" << hi << std::dec
					<< "]";
			}
			if (count > limit) {
				stream << "...";
			}
			return stream.str();
		}

		std::string DescribeSlotTable(uint8_t* lobbyManager) {
			std::ostringstream stream;
			if (!lobbyManager) {
				return "<null>";
			}
			const int addonCount = CharacterConfig::Instance().GetAddonCount();
			const int firstAddonSlot = BASE_CHAR_COUNT + 1;
			const int firstWorkshopSlot = firstAddonSlot + addonCount;
			uint32_t* slots = reinterpret_cast<uint32_t*>(lobbyManager) + kSkinEntryByIndexOffsetDwords;
			uint32_t begin = *reinterpret_cast<uint32_t*>(lobbyManager + 0x38);

			for (int i = 0; i < addonCount + WORKSHOP_CHAR_COUNT; ++i) {
				const int slot = firstAddonSlot + i;
				uint32_t ptr = slots[slot];
				int entryIndex = -1;
				uint8_t active = 0xFF;
				uint32_t type = 0;
				uint32_t lo = 0;
				uint32_t hi = 0;
				if (begin && ptr) {
					entryIndex = static_cast<int>((ptr - begin) / kCharacterArrayEntrySize);
					active = *reinterpret_cast<uint8_t*>(ptr);
					type = *reinterpret_cast<uint32_t*>(ptr + 28);
					lo = *reinterpret_cast<uint32_t*>(ptr + 32);
					hi = *reinterpret_cast<uint32_t*>(ptr + 36);
				}
				stream << "[slot=" << slot
					<< " ptr=0x" << std::hex << ptr << std::dec
					<< " entry=" << entryIndex
					<< " active=" << static_cast<int>(active)
					<< " desc={0x" << std::hex << type << ",0x" << lo << ",0x" << hi << std::dec << "}]";
			}
			stream << " first_workshop_slot=" << firstWorkshopSlot;
			return stream.str();
		}
	}

	char __fastcall DetouredSyncCharacterListFromSave(uint8_t* lobbyManager, void*, uint32_t* saveVector, char mode) {
		static int callCount = 0;
		++callCount;
		const bool log = callCount <= 40 || (callCount % 50) == 0;

		if (log) {
			Util::Logger::Instance().Get()->info(
				"[Save][SyncWorkshopDiag] ENTER call={} lobby=0x{:X} save_vector=0x{:X} mode={} vector_count={} char_count_before={} vector={}.",
				callCount,
				reinterpret_cast<uintptr_t>(lobbyManager),
				reinterpret_cast<uintptr_t>(saveVector),
				static_cast<int>(mode),
				VectorCount(saveVector),
				CharacterArrayCount(lobbyManager),
				DescribeSaveVector(saveVector));
		}

		char result = g_originalSyncCharacterListFromSave(lobbyManager, saveVector, mode);

		if (log) {
			Util::Logger::Instance().Get()->info(
				"[Save][SyncWorkshopDiag] LEAVE call={} result={} vector_count={} char_count_after={} slots={}.",
				callCount,
				static_cast<int>(result),
				VectorCount(saveVector),
				CharacterArrayCount(lobbyManager),
				DescribeSlotTable(lobbyManager));
		}

		return result;
	}

	bool SetupSyncCharacterListFromSaveHook() {
		g_originalSyncCharacterListFromSave = reinterpret_cast<SyncCharacterListFromSaveFn>(g_moduleBase + kSyncCharacterListFromSaveRva);

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		LONG attachResult = DetourAttach(&(PVOID&)g_originalSyncCharacterListFromSave, DetouredSyncCharacterListFromSave);
		if (attachResult != NO_ERROR) {
			Util::Logger::Instance().Get()->error("[Save] SyncCharacterListFromSave DetourAttach failed: {}.", attachResult);
			DetourTransactionAbort();
			return false;
		}

		LONG commitResult = DetourTransactionCommit();
		if (commitResult != NO_ERROR) {
			Util::Logger::Instance().Get()->error("[Save] SyncCharacterListFromSave DetourTransactionCommit failed: {}.", commitResult);
			return false;
		}

		Util::Logger::Instance().Get()->info("[Save] SyncCharacterListFromSave hook attached at rva=0x{:X}.", kSyncCharacterListFromSaveRva);
		return true;
	}
}
