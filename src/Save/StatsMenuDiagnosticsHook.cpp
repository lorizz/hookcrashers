#include "StatsMenuDiagnosticsHook.h"

#include "../Util/Logger.h"
#include "../../include/HookCrashers/Public/Globals.h"

#include <windows.h>
#include <detours.h>
#include <cstdint>
#include <intrin.h>

namespace HookCrashers::Save {
namespace {
    constexpr uintptr_t kGotoAndStopRva = 0x11A250;
    constexpr uintptr_t kStatsInitGotoReturnRva = 0x14A69C;
    constexpr uintptr_t kStatsInitBeginRva = 0x14A5F0;
    constexpr uintptr_t kStatsInitEndRva = 0x14A96A;
    constexpr bool kEnableStatsMenuDiagnosticsHook = true;

    using GotoAndStopFn = int(__thiscall*)(void* clip, int frame);
    using GetChildrenFn = int*(__thiscall*)(void* clip);

    GotoAndStopFn g_originalGotoAndStop = nullptr;

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
        const uintptr_t end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        return begin + bytes >= begin && begin + bytes <= end;
    }

    template <typename T>
    T Read(const void* base, size_t offset, T fallback = {}) {
        if (!base) {
            return fallback;
        }
        const auto* ptr = reinterpret_cast<const uint8_t*>(base) + offset;
        if (!IsReadablePointer(ptr, sizeof(T))) {
            return fallback;
        }
        return *reinterpret_cast<const T*>(ptr);
    }

    GetChildrenFn GetChildrenMethod(void* clip) {
        if (!clip || !IsReadablePointer(clip, sizeof(uintptr_t))) {
            return nullptr;
        }
        const uintptr_t vtable = Read<uintptr_t>(clip, 0, 0);
        if (!vtable || !IsReadablePointer(reinterpret_cast<void*>(vtable + 84), sizeof(uintptr_t))) {
            return nullptr;
        }
        return *reinterpret_cast<GetChildrenFn*>(vtable + 84);
    }

    void LogChildren(const char* label, void* clip, uintptr_t ret, int requestedFrame, int gotoResult) {
        auto log = Util::Logger::Instance().Get();
        if (!clip) {
            log->warn("[StatsMenuDiag] {} clip=null ret=0x{:X} requested_frame={} result={}", label, ret, requestedFrame, gotoResult);
            return;
        }

        const uint16_t currentFrame = Read<uint16_t>(clip, 242, 0);
        const uint16_t frameCount = Read<uint16_t>(clip, 244, 0);
        const uint8_t flags = Read<uint8_t>(clip, 236, 0);
        const uint16_t charId = Read<uint16_t>(clip, 176, 0);
        const uint16_t nameId = Read<uint16_t>(clip, 178, 0);
        GetChildrenFn getChildren = GetChildrenMethod(clip);
        int* childrenList = getChildren ? getChildren(clip) : nullptr;
        int child = (childrenList && IsReadablePointer(childrenList, sizeof(int))) ? *childrenList : 0;

        log->info(
            "[StatsMenuDiag] {} after GotoAndStop ret=0x{:X} clip=0x{:X} char_id={} name_id={} frame={}/{} requested={} flags=0x{:02X} result={} children_list=0x{:X} first_child=0x{:X}.",
            label,
            ret,
            reinterpret_cast<uintptr_t>(clip),
            charId,
            nameId,
            currentFrame,
            frameCount,
            requestedFrame,
            static_cast<unsigned int>(flags),
            gotoResult,
            reinterpret_cast<uintptr_t>(childrenList),
            static_cast<uintptr_t>(child));

        int count = 0;
        while (child && count < 64) {
            if (!IsReadablePointer(reinterpret_cast<void*>(child), 248)) {
                log->warn("[StatsMenuDiag] {} child[{}]=0x{:X} unreadable; stopping.", label, count, static_cast<uintptr_t>(child));
                break;
            }
            const uint16_t childCharId = Read<uint16_t>(reinterpret_cast<void*>(child), 176, 0);
            const uint16_t childNameId = Read<uint16_t>(reinterpret_cast<void*>(child), 178, 0);
            const uint32_t flags24 = Read<uint32_t>(reinterpret_cast<void*>(child), 24, 0);
            const uint32_t depthOrLayer = Read<uint32_t>(reinterpret_cast<void*>(child), 32, 0);
            const uint16_t childFrame = Read<uint16_t>(reinterpret_cast<void*>(child), 242, 0);
            const uint16_t childFrames = Read<uint16_t>(reinterpret_cast<void*>(child), 244, 0);
            const uint8_t childFlags = Read<uint8_t>(reinterpret_cast<void*>(child), 236, 0);
            const int next = Read<int>(reinterpret_cast<void*>(child), 188, 0);
            log->info(
                "[StatsMenuDiag] {} child[{}] ptr=0x{:X} char_id={} name_id={} depth_or_layer={} flags24=0x{:X} frame={}/{} flags=0x{:02X} next=0x{:X}.",
                label,
                count,
                static_cast<uintptr_t>(child),
                childCharId,
                childNameId,
                depthOrLayer,
                flags24,
                childFrame,
                childFrames,
                static_cast<unsigned int>(childFlags),
                static_cast<uintptr_t>(next));
            child = next;
            ++count;
        }
        log->info("[StatsMenuDiag] {} child_count_sample={}", label, count);
    }

    int __fastcall DetouredGotoAndStop(void* clip, void*, int frame) {
        const uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
        const int result = g_originalGotoAndStop(clip, frame);
        const uintptr_t relRet = ret >= g_moduleBase ? ret - g_moduleBase : ret;

        if (relRet == kStatsInitGotoReturnRva) {
            LogChildren("stats_init_root_frame2", clip, relRet, frame, result);
        }
        else if (relRet >= kStatsInitBeginRva && relRet <= kStatsInitEndRva) {
            static int s_extraStatsLogs = 0;
            if (s_extraStatsLogs < 16) {
                ++s_extraStatsLogs;
                LogChildren("stats_init_other_goto", clip, relRet, frame, result);
            }
        }
        return result;
    }
}

bool SetupStatsMenuDiagnosticsHook() {
    if (!kEnableStatsMenuDiagnosticsHook) {
        Util::Logger::Instance().Get()->debug("[StatsMenuDiag] diagnostics hook disabled.");
        return true;
    }

    g_originalGotoAndStop = reinterpret_cast<GotoAndStopFn>(g_moduleBase + kGotoAndStopRva);

    Util::Logger::Instance().Get()->info(
        "[StatsMenuDiag] Installing GotoAndStop hook | RVA=0x{:X} | VA=0x{:X} | stats_ret=0x{:X}.",
        kGotoAndStopRva,
        reinterpret_cast<uintptr_t>(g_originalGotoAndStop),
        kStatsInitGotoReturnRva);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG attachResult = DetourAttach(&(PVOID&)g_originalGotoAndStop, DetouredGotoAndStop);
    if (attachResult != NO_ERROR) {
        Util::Logger::Instance().Get()->error("[StatsMenuDiag] GotoAndStop DetourAttach failed: {}.", attachResult);
        DetourTransactionAbort();
        return false;
    }

    LONG commitResult = DetourTransactionCommit();
    if (commitResult != NO_ERROR) {
        Util::Logger::Instance().Get()->error("[StatsMenuDiag] GotoAndStop DetourTransactionCommit failed: {}.", commitResult);
        return false;
    }

    Util::Logger::Instance().Get()->info("[StatsMenuDiag] GotoAndStop diagnostics hook attached successfully.");
    return true;
}
}

