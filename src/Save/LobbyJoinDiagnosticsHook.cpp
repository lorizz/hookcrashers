#include "LobbyJoinDiagnosticsHook.h"
#include "../Util/Logger.h"
#include "../../include/HookCrashers/Public/Globals.h"
#include <windows.h>
#include <detours.h>
#include <cstdint>

namespace HookCrashers::Save {
namespace {
constexpr uintptr_t kTryFinalizeLobbyJoinRva = 0xD24B0;
constexpr uintptr_t kValidateLobbyPlayerSlotRva = 0xD2B30;
constexpr uintptr_t kGlobalNetworkEventReasonRva = 0x260E94;
using TryFinalizeLobbyJoinFn = int*(__thiscall*)(uint32_t* self, int* outResult, char a3);
using ValidateLobbyPlayerSlotFn = int*(__thiscall*)(int* outResult, uint32_t slot);
TryFinalizeLobbyJoinFn g_originalTryFinalizeLobbyJoin = nullptr;
ValidateLobbyPlayerSlotFn g_originalValidateLobbyPlayerSlot = nullptr;

uint32_t ReadU32(const void* base, size_t offset) {
    return base ? *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(base) + offset) : 0;
}
uint8_t ReadU8(const void* base, size_t offset) {
    return base ? *reinterpret_cast<const uint8_t*>(reinterpret_cast<const uint8_t*>(base) + offset) : 0;
}
uint32_t GlobalReason() {
    return *reinterpret_cast<uint32_t*>(g_moduleBase + kGlobalNetworkEventReasonRva);
}


uint8_t CountPresentSlots(const uint32_t* self) {
    if (!self) {
        return 0;
    }
    uint8_t count = 0;
    for (size_t index = 11; index <= 14; ++index) {
        if (self[index]) {
            ++count;
        }
    }
    return count;
}

uint8_t& PlayerSlotCount(uint32_t* self) {
    return *reinterpret_cast<uint8_t*>(reinterpret_cast<uint8_t*>(self) + 0x11C);
}

int* __fastcall DetouredValidateLobbyPlayerSlot(int* outResult, void*, uint32_t slot) {
    const uint32_t slotVtable = slot ? ReadU32(reinterpret_cast<void*>(slot), 0) : 0;
    int* result = g_originalValidateLobbyPlayerSlot(outResult, slot);
    const uint32_t result0 = outResult ? static_cast<uint32_t>(outResult[0]) : 0xFFFFFFFF;
    const uint32_t result1 = outResult ? static_cast<uint32_t>(outResult[1]) : 0xFFFFFFFF;
    Util::Logger::Instance().Get()->info("[LobbyJoin] ValidateSlot slot=0x{:X} vtable=0x{:X} out0=0x{:X} out1=0x{:X} ret=0x{:X}.", slot, slotVtable, result0, result1, reinterpret_cast<uintptr_t>(result));
    return result;
}

int* __fastcall DetouredTryFinalizeLobbyJoin(uint32_t* self, void*, int* outResult, char a3) {
    const uint32_t stateBefore = self ? self[2] : 0xFFFFFFFF;
    const uint32_t modeBefore = self ? self[6] : 0xFFFFFFFF;
    const uint8_t flags286Before = self ? ReadU8(self, 286) : 0xFF;
    const uint32_t listener = self ? self[5] : 0;
    const uint32_t slot0 = self ? self[11] : 0;
    const uint32_t slot1 = self ? self[12] : 0;
    const uint32_t slot2 = self ? self[13] : 0;
    const uint32_t slot3 = self ? self[14] : 0;
    const uint8_t storedSlotCountBefore = self ? PlayerSlotCount(self) : 0xFF;
    const uint8_t actualSlotCount = CountPresentSlots(self);
    if (self && storedSlotCountBefore == 0 && actualSlotCount != 0) {
        PlayerSlotCount(self) = actualSlotCount;
        Util::Logger::Instance().Get()->warn("[LobbyJoin] Repaired stale slot count self=0x{:X} stored={} actual={} slots=[0x{:X},0x{:X},0x{:X},0x{:X}].", reinterpret_cast<uintptr_t>(self), storedSlotCountBefore, actualSlotCount, slot0, slot1, slot2, slot3);
    }
    const uint8_t storedSlotCountAfterRepair = self ? PlayerSlotCount(self) : 0xFF;
    Util::Logger::Instance().Get()->info("[LobbyJoin] TryFinalize enter self=0x{:X} out=0x{:X} a3={} state={} mode={} reason={} flags286=0x{:02X} slot_count {}->{} actual={} listener=0x{:X} slots=[0x{:X},0x{:X},0x{:X},0x{:X}].", reinterpret_cast<uintptr_t>(self), reinterpret_cast<uintptr_t>(outResult), static_cast<int>(a3), stateBefore, modeBefore, GlobalReason(), flags286Before, storedSlotCountBefore, storedSlotCountAfterRepair, actualSlotCount, listener, slot0, slot1, slot2, slot3);
    int* result = g_originalTryFinalizeLobbyJoin(self, outResult, a3);
    const uint32_t stateAfter = self ? self[2] : 0xFFFFFFFF;
    const uint32_t modeAfter = self ? self[6] : 0xFFFFFFFF;
    const uint8_t flags286After = self ? ReadU8(self, 286) : 0xFF;
    const uint32_t out0 = outResult ? static_cast<uint32_t>(outResult[0]) : 0xFFFFFFFF;
    const uint32_t out1 = outResult ? static_cast<uint32_t>(outResult[1]) : 0xFFFFFFFF;
    Util::Logger::Instance().Get()->info("[LobbyJoin] TryFinalize leave self=0x{:X} state {}->{} mode {}->{} reason={} flags286 0x{:02X}->0x{:02X} out=[0x{:X},0x{:X}] ret=0x{:X}.", reinterpret_cast<uintptr_t>(self), stateBefore, stateAfter, modeBefore, modeAfter, GlobalReason(), flags286Before, flags286After, out0, out1, reinterpret_cast<uintptr_t>(result));
    return result;
}

}

bool SetupLobbyJoinDiagnosticsHook() {
    g_originalTryFinalizeLobbyJoin = reinterpret_cast<TryFinalizeLobbyJoinFn>(g_moduleBase + kTryFinalizeLobbyJoinRva);
    g_originalValidateLobbyPlayerSlot = reinterpret_cast<ValidateLobbyPlayerSlotFn>(g_moduleBase + kValidateLobbyPlayerSlotRva);
    Util::Logger::Instance().Get()->info("[LobbyJoin] Installing hooks | finalize RVA=0x{:X} VA=0x{:X} | validate RVA=0x{:X} VA=0x{:X}.", kTryFinalizeLobbyJoinRva, reinterpret_cast<uintptr_t>(g_originalTryFinalizeLobbyJoin), kValidateLobbyPlayerSlotRva, reinterpret_cast<uintptr_t>(g_originalValidateLobbyPlayerSlot));
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG attachResult = DetourAttach(&(PVOID&)g_originalTryFinalizeLobbyJoin, DetouredTryFinalizeLobbyJoin);
    // sub_1D2B30 has unstable recovered calling convention; keep this hook disabled to avoid corrupting the lobby path.
    if (attachResult != NO_ERROR) {
        Util::Logger::Instance().Get()->error("[LobbyJoin] DetourAttach failed: {}.", attachResult);
        DetourTransactionAbort();
        return false;
    }
    const LONG commitResult = DetourTransactionCommit();
    if (commitResult != NO_ERROR) {
        Util::Logger::Instance().Get()->error("[LobbyJoin] DetourTransactionCommit failed: {}.", commitResult);
        return false;
    }
    Util::Logger::Instance().Get()->info("[LobbyJoin] diagnostics hooks attached successfully.");
    return true;
}

}


