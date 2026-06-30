#include "NetworkCleanupDiagnosticsHook.h"

#include "../Util/Logger.h"
#include "../../include/HookCrashers/Public/Globals.h"

#include <windows.h>
#include <detours.h>
#include <cstdint>

namespace HookCrashers::Save {
	namespace {
		constexpr uintptr_t kCleanupObjectListRva = 0x11D860;
		constexpr bool kEnableCleanupDiagnosticsHook = false;
		constexpr const char* kGreen = "";
		constexpr const char* kYellow = "";
		constexpr const char* kReset = "";

		using CleanupObjectListFn = int(__thiscall*)(uint32_t* self);
		CleanupObjectListFn g_originalCleanupObjectList = nullptr;

		bool IsReadablePointer(const void* ptr, size_t bytes) {
			if (!ptr || bytes == 0) {
				return false;
			}

			MEMORY_BASIC_INFORMATION mbi{};
			if (!VirtualQuery(ptr, &mbi, sizeof(mbi))) {
				return false;
			}

			if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
				return false;
			}

			const uintptr_t begin = reinterpret_cast<uintptr_t>(ptr);
			const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
			return begin + bytes >= begin && begin + bytes <= regionEnd;
		}

		uint32_t ReadU32Safe(const void* ptr, bool& ok) {
			ok = IsReadablePointer(ptr, sizeof(uint32_t));
			if (!ok) {
				return 0;
			}
			return *reinterpret_cast<const uint32_t*>(ptr);
		}

		void LogCleanupList(uint32_t* self) {
			auto log = Util::Logger::Instance().Get();
			if (!self) {
				log->warn("{}[NetworkCleanup] sub_21D860 self=null.{}", kYellow, kReset);
				return;
			}

			const bool headerOk = IsReadablePointer(self, sizeof(uint32_t) * 4);
			if (!headerOk) {
				log->warn(
					"{}[NetworkCleanup] sub_21D860 unreadable self=0x{:X}.{}",
					kYellow,
					reinterpret_cast<uintptr_t>(self),
					kReset);
				return;
			}

			const uint32_t count = self[1];
			const uintptr_t entries = self[2];
			const uint32_t capacityOrState = self[3];
			const bool entriesOk = count == 0 || IsReadablePointer(reinterpret_cast<void*>(entries), static_cast<size_t>(count) * 8);

			log->debug(
				"{}[NetworkCleanup] sub_21D860 enter self=0x{:X} count={} entries=0x{:X} state={} entries_ok={}.{}",
				kGreen,
				reinterpret_cast<uintptr_t>(self),
				count,
				entries,
				capacityOrState,
				entriesOk,
				kReset);

			if (!entriesOk || count > 512) {
				log->warn(
					"{}[NetworkCleanup] suspicious cleanup list self=0x{:X} count={} entries=0x{:X}; original will still run.{}",
					kYellow,
					reinterpret_cast<uintptr_t>(self),
					count,
					entries,
					kReset);
				return;
			}

			const uint32_t sampleCount = count < 16 ? count : 16;
			for (uint32_t i = 0; i < sampleCount; ++i) {
				const uintptr_t itemRecord = entries + static_cast<uintptr_t>(i) * 8;
				bool recordPtrOk = false;
				const uint32_t objectRecord = ReadU32Safe(reinterpret_cast<void*>(itemRecord + 4), recordPtrOk);
				bool objectRecordOk = false;
				const uint32_t objectPtr = ReadU32Safe(reinterpret_cast<void*>(objectRecord), objectRecordOk);
				bool objectOk = false;
				const uint32_t vtable = ReadU32Safe(reinterpret_cast<void*>(objectPtr), objectOk);
				const bool suspicious = !recordPtrOk || !objectRecord || !objectRecordOk || (objectPtr && !objectOk);

				if (suspicious || i < 4) {
					log->debug(
						"{}[NetworkCleanup] entry[{}] item=0x{:X} record_ok={} record=0x{:X} object_ok={} object=0x{:X} vtable_ok={} vtable=0x{:X}.{}",
						suspicious ? kYellow : kGreen,
						i,
						itemRecord,
						recordPtrOk,
						objectRecord,
						objectRecordOk,
						objectPtr,
						objectOk,
						vtable,
						kReset);
				}
			}
		}

		int __fastcall DetouredCleanupObjectList(uint32_t* self, void*) {
			LogCleanupList(self);
			return g_originalCleanupObjectList(self);
		}
	}

	bool SetupNetworkCleanupDiagnosticsHook() {
		if (!kEnableCleanupDiagnosticsHook) {
			Util::Logger::Instance().Get()->debug("[NetworkCleanup] sub_21D860 diagnostics hook disabled.");
			return true;
		}

		g_originalCleanupObjectList = reinterpret_cast<CleanupObjectListFn>(g_moduleBase + kCleanupObjectListRva);

		Util::Logger::Instance().Get()->debug(
			"[NetworkCleanup] Installing hook | name=sub_21D860 | RVA=0x{:X} | VA=0x{:X}.",
			kCleanupObjectListRva,
			reinterpret_cast<uintptr_t>(g_originalCleanupObjectList));

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		LONG attachResult = DetourAttach(&(PVOID&)g_originalCleanupObjectList, DetouredCleanupObjectList);
		if (attachResult != NO_ERROR) {
			Util::Logger::Instance().Get()->error("[NetworkCleanup] sub_21D860 DetourAttach failed: {}.", attachResult);
			DetourTransactionAbort();
			return false;
		}

		LONG commitResult = DetourTransactionCommit();
		if (commitResult != NO_ERROR) {
			Util::Logger::Instance().Get()->error("[NetworkCleanup] sub_21D860 DetourTransactionCommit failed: {}.", commitResult);
			return false;
		}

		Util::Logger::Instance().Get()->debug("[NetworkCleanup] sub_21D860 diagnostics hook attached successfully.");
		return true;
	}
}


