#include "RebuildCharacterSlotTableHook.h"

#include "CharacterConfig.h"
#include "../Util/Logger.h"
#include "../../include/HookCrashers/Public/Globals.h"

#include <windows.h>
#include <detours.h>
#include <cstdint>
#include <sstream>

namespace HookCrashers::Save {
	namespace {
		constexpr uintptr_t kRebuildCharacterSlotTableRva = 0x846D0; // updated
		constexpr int kCharacterArrayEntrySize = 0x1090;
		constexpr int kSkinEntryByIndexOffsetDwords = 1130;

		using RebuildCharacterSlotTableFn = char(__thiscall*)(uint32_t* lobbyManager, bool includeClientWorkshop);
		RebuildCharacterSlotTableFn g_originalRebuildCharacterSlotTable = nullptr;

		uint32_t* GetSkinEntryByIndex(uint32_t* lobbyManager) {
			return lobbyManager + kSkinEntryByIndexOffsetDwords;
		}

		uint8_t* GetCharacterArrayBegin(uint32_t* lobbyManager) {
			return reinterpret_cast<uint8_t*>(lobbyManager[14]);
		}

		uint8_t* GetCharacterArrayCurrent(uint32_t* lobbyManager) {
			return reinterpret_cast<uint8_t*>(lobbyManager[15]);
		}

		uint8_t* GetCharacterEntry(uint8_t* begin, int index) {
			return begin + (index * kCharacterArrayEntrySize);
		}

		bool HasCharacterEntry(uint8_t* begin, uint8_t* current, int index) {
			const uint8_t* entry = GetCharacterEntry(begin, index);
			return begin && current && entry >= begin && entry < current;
		}

		int GetCharacterArrayCount(uint8_t* begin, uint8_t* current) {
			if (!begin || !current || current < begin) {
				return 0;
			}
			return static_cast<int>((current - begin) / kCharacterArrayEntrySize);
		}

		bool IsCharacterEntryInRange(uint8_t* begin, uint8_t* current, uint8_t* entry, int firstIndex, int count) {
			if (!begin || !current || !entry || entry < begin || entry >= current) {
				return false;
			}
			const ptrdiff_t index = (entry - begin) / kCharacterArrayEntrySize;
			return index >= firstIndex && index < firstIndex + count;
		}

		bool IsPointerInCharacterArray(uint8_t* begin, uint8_t* current, uint8_t* entry) {
			if (!begin || !current || !entry || entry < begin || entry >= current) {
				return false;
			}
			return ((entry - begin) % kCharacterArrayEntrySize) == 0;
		}

		void AddUniqueEntry(uint32_t* entries, int& count, uint32_t value) {
			if (!value || count >= WORKSHOP_CHAR_COUNT * 4) {
				return;
			}
			for (int i = 0; i < count; ++i) {
				if (entries[i] == value) {
					return;
				}
			}
			entries[count++] = value;
		}

		std::string DescribeSlotTable(uint32_t* lobbyManager, int firstSlot, int count) {
			std::ostringstream stream;
			if (!lobbyManager) {
				return "<null-lobby>";
			}

			uint32_t* skinEntryByIndex = GetSkinEntryByIndex(lobbyManager);
			uint8_t* begin = GetCharacterArrayBegin(lobbyManager);
			for (int i = 0; i < count; ++i) {
				const int slot = firstSlot + i;
				uint8_t* ptr = reinterpret_cast<uint8_t*>(skinEntryByIndex[slot]);
				ptrdiff_t entryIndex = -1;
				uint8_t active = 0xFF;
				uint32_t descLo = 0;
				uint32_t descHi = 0;
				uint32_t descType = 0;

				if (begin && ptr) {
					entryIndex = (ptr - begin) / kCharacterArrayEntrySize;
					active = *ptr;
					descType = *reinterpret_cast<uint32_t*>(ptr + 28);
					descLo = *reinterpret_cast<uint32_t*>(ptr + 32);
					descHi = *reinterpret_cast<uint32_t*>(ptr + 36);
				}

				stream << "[slot=" << slot
					<< " ptr=0x" << std::hex << reinterpret_cast<uintptr_t>(ptr) << std::dec
					<< " entry=" << entryIndex
					<< " active=" << static_cast<int>(active)
					<< " desc={type=0x" << std::hex << descType
					<< ",lo=0x" << descLo
					<< ",hi=0x" << descHi << std::dec
					<< "}]";
			}
			return stream.str();
		}

		std::string DescribeArrayRange(uint32_t* lobbyManager, int firstEntry, int count) {
			std::ostringstream stream;
			if (!lobbyManager) {
				return "<null-lobby>";
			}

			uint8_t* begin = GetCharacterArrayBegin(lobbyManager);
			uint8_t* current = GetCharacterArrayCurrent(lobbyManager);
			for (int i = 0; i < count; ++i) {
				const int index = firstEntry + i;
				uint8_t* ptr = HasCharacterEntry(begin, current, index) ? GetCharacterEntry(begin, index) : nullptr;
				uint8_t active = ptr ? *ptr : 0xFF;
				uint32_t descType = ptr ? *reinterpret_cast<uint32_t*>(ptr + 28) : 0;
				uint32_t descLo = ptr ? *reinterpret_cast<uint32_t*>(ptr + 32) : 0;
				uint32_t descHi = ptr ? *reinterpret_cast<uint32_t*>(ptr + 36) : 0;
				uint32_t pathLen = ptr ? *reinterpret_cast<uint32_t*>(ptr + 48) : 0;
				uint32_t pathCap = ptr ? *reinterpret_cast<uint32_t*>(ptr + 56) : 0;

				stream << "[entry=" << index
					<< " ptr=0x" << std::hex << reinterpret_cast<uintptr_t>(ptr) << std::dec
					<< " active=" << static_cast<int>(active)
					<< " desc={type=0x" << std::hex << descType
					<< ",lo=0x" << descLo
					<< ",hi=0x" << descHi << std::dec
					<< "} path_len=" << pathLen
					<< " path_cap=" << pathCap
					<< "]";
			}
			return stream.str();
		}

		void FixExpandedCharacterSlotTable(uint32_t* lobbyManager) {
			const int addonCount = CharacterConfig::Instance().GetAddonCount();
			if (!lobbyManager || addonCount <= 0) {
				return;
			}

			uint8_t* begin = GetCharacterArrayBegin(lobbyManager);
			uint8_t* current = GetCharacterArrayCurrent(lobbyManager);
			if (!begin || !current || current < begin) {
				return;
			}

			uint32_t* skinEntryByIndex = GetSkinEntryByIndex(lobbyManager);
			const int expandedBaseCount = BASE_CHAR_COUNT + addonCount;
			const int firstAddonSlot = BASE_CHAR_COUNT + 1;
			const int firstWorkshopSlot = firstAddonSlot + addonCount;
			const int characterArrayCount = GetCharacterArrayCount(begin, current);
			uint32_t workshopEntries[WORKSHOP_CHAR_COUNT * 4] = {};
			int workshopEntryCount = 0;

			// Vanilla writes workshop/materialized entries starting at slot 33. Addon
			// slots are inserted before that range, so collect only the vanilla workshop
			// window and move those entries to the shifted workshop range.
			for (int i = 0; i < WORKSHOP_CHAR_COUNT * 4; ++i) {
				const int slot = firstAddonSlot + i;
				uint8_t* existing = reinterpret_cast<uint8_t*>(skinEntryByIndex[slot]);
				if (IsPointerInCharacterArray(begin, current, existing) && *existing) {
					AddUniqueEntry(workshopEntries, workshopEntryCount, reinterpret_cast<uint32_t>(existing));
				}
			}

			static int s_verboseLogCount = 0;
			const bool verboseLog = s_verboseLogCount < 24;
			if (verboseLog) {
				++s_verboseLogCount;
				Util::Logger::Instance().Get()->info(
					"[Save][RebuildDiag] before addon_count={} expanded_base={} first_addon_slot={} first_workshop_slot={} array_count={} array_begin=0x{:X} array_current=0x{:X} table_addons={} table_workshops={} array_addons={} array_workshops={}.",
					addonCount,
					expandedBaseCount,
					firstAddonSlot,
					firstWorkshopSlot,
					characterArrayCount,
					reinterpret_cast<uintptr_t>(begin),
					reinterpret_cast<uintptr_t>(current),
					DescribeSlotTable(lobbyManager, firstAddonSlot, addonCount),
					DescribeSlotTable(lobbyManager, firstWorkshopSlot, WORKSHOP_CHAR_COUNT),
					DescribeArrayRange(lobbyManager, BASE_CHAR_COUNT, addonCount),
					DescribeArrayRange(lobbyManager, expandedBaseCount, WORKSHOP_CHAR_COUNT));
			}

			for (int i = 0; i < addonCount; ++i) {
				skinEntryByIndex[firstAddonSlot + i] = 0;
			}
			for (int i = 0; i < WORKSHOP_CHAR_COUNT * 4; ++i) {
				skinEntryByIndex[firstWorkshopSlot + i] = 0;
			}

			for (int i = 0; i < workshopEntryCount; ++i) {
				skinEntryByIndex[firstWorkshopSlot + i] = workshopEntries[i];
			}

			if (verboseLog) {
				Util::Logger::Instance().Get()->info(
					"[Save][RebuildDiag] after table_addons={} table_workshops={}.",
					DescribeSlotTable(lobbyManager, firstAddonSlot, addonCount),
					DescribeSlotTable(lobbyManager, firstWorkshopSlot, WORKSHOP_CHAR_COUNT));
			}

			static int s_logCount = 0;
			if (s_logCount < 8) {
				++s_logCount;
				Util::Logger::Instance().Get()->info(
					"[Save] RebuildCharacterSlotTable post-fix applied addon_count={} first_addon_slot={} first_workshop_slot={} array_begin=0x{:X} array_current=0x{:X}.",
					addonCount,
					firstAddonSlot,
					firstWorkshopSlot,
					reinterpret_cast<uintptr_t>(begin),
					reinterpret_cast<uintptr_t>(current));
			}
		}
	}

	char __fastcall DetouredRebuildCharacterSlotTable(uint32_t* lobbyManager, void*, bool includeClientWorkshop) {
		static int s_callCount = 0;
		++s_callCount;
		Util::Logger::Instance().Get()->info(
			"[HookHit] RebuildCharacterSlotTable ENTER call={} lobby=0x{:X} include_client_workshop={}",
			s_callCount,
			reinterpret_cast<uintptr_t>(lobbyManager),
			includeClientWorkshop);
		Util::Logger::Instance().Get()->flush();

		char result = g_originalRebuildCharacterSlotTable(lobbyManager, includeClientWorkshop);
		Util::Logger::Instance().Get()->info("[HookHit] RebuildCharacterSlotTable LEAVE original call={} result={}", s_callCount, static_cast<int>(result));
		Util::Logger::Instance().Get()->flush();

		FixExpandedCharacterSlotTable(lobbyManager);
		Util::Logger::Instance().Get()->info("[HookHit] RebuildCharacterSlotTable LEAVE fix call={}", s_callCount);
		Util::Logger::Instance().Get()->flush();
		return result;
	}

	bool SetupRebuildCharacterSlotTableHook() {
		g_originalRebuildCharacterSlotTable = reinterpret_cast<RebuildCharacterSlotTableFn>(g_moduleBase + kRebuildCharacterSlotTableRva);

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		LONG attachResult = DetourAttach(&(PVOID&)g_originalRebuildCharacterSlotTable, DetouredRebuildCharacterSlotTable);
		if (attachResult != NO_ERROR) {
			Util::Logger::Instance().Get()->error("[Save] RebuildCharacterSlotTable DetourAttach failed: {}.", attachResult);
			DetourTransactionAbort();
			return false;
		}

		LONG commitResult = DetourTransactionCommit();
		if (commitResult != NO_ERROR) {
			Util::Logger::Instance().Get()->error("[Save] RebuildCharacterSlotTable DetourTransactionCommit failed: {}.", commitResult);
			return false;
		}

		Util::Logger::Instance().Get()->info("[Save] RebuildCharacterSlotTable hook attached at rva=0x{:X}.", kRebuildCharacterSlotTableRva);
		return true;
	}
}
