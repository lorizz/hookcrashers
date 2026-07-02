#include "AttachSkinGraphicDiagnosticsHook.h"

#include "CharacterConfig.h"
#include "../Util/Logger.h"
#include "../../include/HookCrashers/Public/Globals.h"

#include <windows.h>
#include <detours.h>
#include <cstdint>
#include <atomic>

namespace HookCrashers::Save {
namespace {
	constexpr uintptr_t kAttachSkinGraphicRva = 0x8D2E0;
	using AttachSkinGraphicFn = int(__thiscall*)(void* self, void* clip, int swfObject, int partId);

	AttachSkinGraphicFn g_originalAttachSkinGraphic = nullptr;
	std::atomic<uint32_t> g_attachPortraitSequence{ 0 };

	template <typename T>
	T ReadValue(const void* base, size_t offset, T fallback = {}) {
		if (!base) {
			return fallback;
		}
		return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(base) + offset);
	}

	int NormalizeFallback(unsigned int frame) {
		unsigned int value = frame - 1;
		if (frame == 3) {
			value = 3;
		}
		else if (frame == 4) {
			value = 2;
		}
		return value < 0x49 ? static_cast<int>(value) : 0;
	}

	extern "C" __declspec(noinline) void LogAttachSkinGraphic(void* self, void* esiArg, void* swfObject, int partId) {
		const uint8_t enabled = ReadValue<uint8_t>(self, 14660);
		const uint8_t allowWeaponReset = ReadValue<uint8_t>(self, 14666);
		void* workshopContext = ReadValue<void*>(self, 14648, nullptr);
		const int workshopContextType = ReadValue<int>(workshopContext, 56, -1);
		const bool hasWorkshopContext = workshopContext && workshopContextType != -1;
		const uint16_t frame = ReadValue<uint16_t>(swfObject, 242);
		const int freshCandidate = static_cast<int>(frame) - 102;
		const int classicCandidate = static_cast<int>(frame) - 100;
		const uint16_t part36Adjusted = static_cast<uint16_t>(frame - 102);
		const uint16_t part36AdjustedBase103 = static_cast<uint16_t>(frame - 103);
		const uint16_t part37Adjusted = static_cast<uint16_t>(frame - 100);
		const int part36FallbackSlot = NormalizeFallback(part36Adjusted);
		const int part36FallbackSlotBase103 = NormalizeFallback(part36AdjustedBase103);
		const int part37FallbackSlot = NormalizeFallback(part37Adjusted);
		const int fallbackCandidate = NormalizeFallback(frame);
		const int addonCount = CharacterConfig::Instance().GetAddonCount();
		const int workshopStart = BASE_FRESH_COUNT + 1 + addonCount;
		const int totalSlots = TOTAL_ORIGINAL_SLOTS + addonCount;
		const uint32_t sequence = partId == 36 ? ++g_attachPortraitSequence : g_attachPortraitSequence.load();
		const char* lane = "other";
		if (partId == 36) {
			switch ((sequence - 1) % 3) {
			case 0: lane = "top"; break;
			case 1: lane = "mid"; break;
			case 2: lane = "bot"; break;
			}
		}

		Util::Logger::Instance().Get()->info(
			"[AttachSkinGraphic] seq={} lane={} self=0x{:X} esi=0x{:X} swf=0x{:X} part={} frame={} enabled={} ctx=0x{:X} ctx_type={} has_ctx={} allow_weapon_reset={} fresh_calc_base102={} fresh_calc_base103={} classic_calc={} raw_fallback={} base102_adjusted={} base102_slot={} base103_adjusted={} base103_slot={} part37_adjusted={} part37_slot={} workshop_start={} total_slots={}.",
			sequence,
			lane,
			reinterpret_cast<uintptr_t>(self),
			reinterpret_cast<uintptr_t>(esiArg),
			reinterpret_cast<uintptr_t>(swfObject),
			partId,
			frame,
			static_cast<int>(enabled),
			reinterpret_cast<uintptr_t>(workshopContext),
			workshopContextType,
			hasWorkshopContext,
			static_cast<int>(allowWeaponReset),
			freshCandidate,
			static_cast<int>(frame) - 103,
			classicCandidate,
			fallbackCandidate,
			part36Adjusted,
			part36FallbackSlot,
			part36AdjustedBase103,
			part36FallbackSlotBase103,
			part37Adjusted,
			part37FallbackSlot,
			workshopStart,
			totalSlots);
	}

	__declspec(naked) void DetouredAttachSkinGraphic() {
		__asm {
			pushfd
			pushad
			push dword ptr [esp + 44] // partId / a4
			push dword ptr [esp + 44] // swfObject / a3, adjusted after previous push
			push dword ptr [esp + 12] // original esi saved by pushad, adjusted after pushes
			push dword ptr [esp + 36] // original ecx saved by pushad, adjusted after pushes
			call LogAttachSkinGraphic
			add esp, 16
			popad
			popfd
			jmp dword ptr [g_originalAttachSkinGraphic]
		}
	}
}

bool SetupAttachSkinGraphicDiagnosticsHook() {
	g_originalAttachSkinGraphic = reinterpret_cast<AttachSkinGraphicFn>(g_moduleBase + kAttachSkinGraphicRva);
	Util::Logger::Instance().Get()->info(
		"[AttachSkinGraphic] Installing diagnostics hook RVA=0x{:X} VA=0x{:X}.",
		kAttachSkinGraphicRva,
		reinterpret_cast<uintptr_t>(g_originalAttachSkinGraphic));

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	LONG attachResult = DetourAttach(&(PVOID&)g_originalAttachSkinGraphic, DetouredAttachSkinGraphic);
	if (attachResult != NO_ERROR) {
		Util::Logger::Instance().Get()->error("[AttachSkinGraphic] DetourAttach failed: {}.", attachResult);
		DetourTransactionAbort();
		return false;
	}

	LONG commitResult = DetourTransactionCommit();
	if (commitResult != NO_ERROR) {
		Util::Logger::Instance().Get()->error("[AttachSkinGraphic] DetourTransactionCommit failed: {}.", commitResult);
		return false;
	}

	Util::Logger::Instance().Get()->info("[AttachSkinGraphic] Diagnostics hook attached successfully.");
	return true;
}
}