#include "NetworkEventDiagnosticsHook.h"
#include "../Util/Logger.h"
#include "../../include/HookCrashers/Public/Globals.h"
#include <windows.h>
#include <detours.h>
#include <cstdint>
#include <intrin.h>

namespace HookCrashers::Save {
namespace {
constexpr uintptr_t kNetworkEventDispatcherRva = 0xF3C40;
constexpr int kMaxStackFrames = 16;
using NetworkEventDispatcherFn = int(__thiscall*)(uint32_t* self, int a2, int a3, int a4);
NetworkEventDispatcherFn g_originalNetworkEventDispatcher = nullptr;

uint32_t ReadU32(uint32_t* self, size_t offset) {
    return self ? *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(self) + offset) : 0;
}

int __fastcall DetouredNetworkEventDispatcher(uint32_t* self, void*, int a2, int a3, int a4) {
    const uint32_t reasonBefore = ReadU32(self, 0x90);
    const uint32_t state24 = ReadU32(self, 0x18);
    const uintptr_t callerRet = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool interesting = (a4 == 13 || a3 == 0 || a3 == 8 || a3 == 9);
    if (interesting) {
        Util::Logger::Instance().Get()->info("[NetworkEvent] enter self=0x{:X} a2={} a3={} a4={} state24={} pending_before={} caller_ret=0x{:X} caller_rva=0x{:X}.", reinterpret_cast<uintptr_t>(self), a2, a3, a4, state24, reasonBefore, callerRet, callerRet >= g_moduleBase ? callerRet - g_moduleBase : 0);
        void* frames[kMaxStackFrames]{};
        const USHORT count = CaptureStackBackTrace(0, kMaxStackFrames, frames, nullptr);
        for (USHORT i = 0; i < count; ++i) {
            const uintptr_t address = reinterpret_cast<uintptr_t>(frames[i]);
            Util::Logger::Instance().Get()->info("[NetworkEvent] stack[{}]=0x{:X} rva=0x{:X}.", i, address, address >= g_moduleBase ? address - g_moduleBase : 0);
        }
    }
    const int result = g_originalNetworkEventDispatcher(self, a2, a3, a4);
    const uint32_t reasonAfter = ReadU32(self, 0x90);
    if (interesting || reasonAfter != reasonBefore) {
        Util::Logger::Instance().Get()->info("[NetworkEvent] leave self=0x{:X} a2={} a3={} a4={} pending_before={} pending_after={} result={}.", reinterpret_cast<uintptr_t>(self), a2, a3, a4, reasonBefore, reasonAfter, result);
    }
    return result;
}

}

bool SetupNetworkEventDiagnosticsHook() {
    g_originalNetworkEventDispatcher = reinterpret_cast<NetworkEventDispatcherFn>(g_moduleBase + kNetworkEventDispatcherRva);
    Util::Logger::Instance().Get()->info("[NetworkEvent] Installing hook | name=NetworkEventDispatcher | RVA=0x{:X} | VA=0x{:X}.", kNetworkEventDispatcherRva, reinterpret_cast<uintptr_t>(g_originalNetworkEventDispatcher));
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    const LONG attachResult = DetourAttach(&(PVOID&)g_originalNetworkEventDispatcher, DetouredNetworkEventDispatcher);
    if (attachResult != NO_ERROR) {
        Util::Logger::Instance().Get()->error("[NetworkEvent] NetworkEventDispatcher DetourAttach failed: {}.", attachResult);
        DetourTransactionAbort();
        return false;
    }
    const LONG commitResult = DetourTransactionCommit();
    if (commitResult != NO_ERROR) {
        Util::Logger::Instance().Get()->error("[NetworkEvent] NetworkEventDispatcher DetourTransactionCommit failed: {}.", commitResult);
        return false;
    }
    Util::Logger::Instance().Get()->info("[NetworkEvent] NetworkEventDispatcher diagnostics hook attached successfully.");
    return true;
}

}
