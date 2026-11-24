#pragma once

#include <cstdint>
#include <type_traits>

namespace HookCrashers::NativeDB {

inline std::uintptr_t g_GameBase = 0;

inline void SetGameBase(std::uintptr_t base) {
    g_GameBase = base;
}

inline std::uintptr_t GetGameBase() {
    return g_GameBase;
}

// Every entry is a verified function entry point in castle.exe 0x6A4D42BF
// (2026-07-07). Offsets of other builds (including the macOS binary) must not
// be added here: calling a non-entry address crashes the game.
enum class Natives : std::uintptr_t {
    CC_WriteSaveFile = 0xE9280,
    CC_CreateDefaultSave = 0x12F120,
    CC_WriteDefaultKeybindsToSave = 0x12EEE0,
    CC_SanitizeCharacterSaveSlots = 0x12EBD0,
    CC_SetFreshModeEnabled = 0x10A540,
    CC_LoadKeybindsFromSave = 0x12F2C0,
    CC_ClearCharacterSyncPacket = 0x5FDD0,
    CC_CopyIncomingCharacterSyncPayload = 0x5FE90,
    CC_CopySelectedIdsToPerPlayerWorkshopBlocks = 0x5FF00,
    CC_CopySelectedWorkshopIdsToPerPlayerBlocks = 0x5FFB0,
    CC_MergeIncomingCharacterSyncPayload = 0x60060,
    CC_MergeCharacterSyncPacketForRemoteLobby = 0x86750,
    CC_ReconcileLobbyCharacterRuntimeEntries = 0x86DF0,
    CC_ResolveCharacterSkinRuntime = 0x8D0F0,
    CC_ApplyCharacterSyncPacketToRoster = 0x876F0,
    CC_BuildCharacterSyncPacket = 0x88740,
    CC_SendInitialCharacterSyncPacket = 0xF2120,
    CC_HandleIncomingCharacterSyncPacket = 0xF2390,
    CC_SetPendingWorkshopCloneSelection = 0xF2700,
    CC_RequestWorkshopCharacterCloneFromLobby = 0xF27F0,
    CC_HandleWorkshopCloneButtonPressed = 0x160800,
    CC_CollectAvailableWorkshopCharacterIds = 0x89240,
    CC_RequestRemoteWorkshopCharacterDownload = 0x89500,
    CC_ReportBlockedWorkshopCharacterInPacket = 0x89820,
    CC_SyncCharacterListFromSave = 0x85B90,
    CC_SWFSceneParseHeader = 0x1254F0,
    CC_SWFNodeCtorLoadCOK6 = 0xFD840,
    CC_TextMenuItemSetLocalizedText = 0x154210,
    CC_TextMenuItemCtor = 0x153EE0,
};

struct NativeInfo {
    Natives id;
    const char* name;
    std::uintptr_t rva;
};

inline constexpr NativeInfo kNativeInfos[] = {
#define HC_NATIVE_INFO(name) { Natives::name, #name, static_cast<std::uintptr_t>(Natives::name) }
    HC_NATIVE_INFO(CC_WriteSaveFile),
    HC_NATIVE_INFO(CC_CreateDefaultSave),
    HC_NATIVE_INFO(CC_WriteDefaultKeybindsToSave),
    HC_NATIVE_INFO(CC_SanitizeCharacterSaveSlots),
    HC_NATIVE_INFO(CC_SetFreshModeEnabled),
    HC_NATIVE_INFO(CC_LoadKeybindsFromSave),
    HC_NATIVE_INFO(CC_ClearCharacterSyncPacket),
    HC_NATIVE_INFO(CC_CopyIncomingCharacterSyncPayload),
    HC_NATIVE_INFO(CC_CopySelectedIdsToPerPlayerWorkshopBlocks),
    HC_NATIVE_INFO(CC_CopySelectedWorkshopIdsToPerPlayerBlocks),
    HC_NATIVE_INFO(CC_MergeIncomingCharacterSyncPayload),
    HC_NATIVE_INFO(CC_MergeCharacterSyncPacketForRemoteLobby),
    HC_NATIVE_INFO(CC_ReconcileLobbyCharacterRuntimeEntries),
    HC_NATIVE_INFO(CC_ResolveCharacterSkinRuntime),
    HC_NATIVE_INFO(CC_ApplyCharacterSyncPacketToRoster),
    HC_NATIVE_INFO(CC_BuildCharacterSyncPacket),
    HC_NATIVE_INFO(CC_SendInitialCharacterSyncPacket),
    HC_NATIVE_INFO(CC_HandleIncomingCharacterSyncPacket),
    HC_NATIVE_INFO(CC_SetPendingWorkshopCloneSelection),
    HC_NATIVE_INFO(CC_RequestWorkshopCharacterCloneFromLobby),
    HC_NATIVE_INFO(CC_HandleWorkshopCloneButtonPressed),
    HC_NATIVE_INFO(CC_CollectAvailableWorkshopCharacterIds),
    HC_NATIVE_INFO(CC_RequestRemoteWorkshopCharacterDownload),
    HC_NATIVE_INFO(CC_ReportBlockedWorkshopCharacterInPacket),
    HC_NATIVE_INFO(CC_SyncCharacterListFromSave),
    HC_NATIVE_INFO(CC_SWFSceneParseHeader),
    HC_NATIVE_INFO(CC_SWFNodeCtorLoadCOK6),
    HC_NATIVE_INFO(CC_TextMenuItemSetLocalizedText),
    HC_NATIVE_INFO(CC_TextMenuItemCtor),
#undef HC_NATIVE_INFO
};

inline const NativeInfo* FindNativeInfo(Natives native) {
    for (const NativeInfo& info : kNativeInfos) {
        if (info.id == native) {
            return &info;
        }
    }
    return nullptr;
}

inline const NativeInfo* FindNativeInfo(const char* name) {
    if (!name) {
        return nullptr;
    }

    for (const NativeInfo& info : kNativeInfos) {
        const char* a = info.name;
        const char* b = name;
        while (*a && *b && *a == *b) {
            ++a;
            ++b;
        }
        if (*a == '\0' && *b == '\0') {
            return &info;
        }
    }
    return nullptr;
}

inline std::uintptr_t NativeAddress(Natives native) {
    return g_GameBase + static_cast<std::uintptr_t>(native);
}

template <typename Fn>
inline Fn NativePtr(Natives native) {
    static_assert(std::is_pointer_v<Fn>, "NativePtr expects a function pointer type.");
    return reinterpret_cast<Fn>(NativeAddress(native));
}

template <typename Fn, typename... Args>
inline decltype(auto) CallNative(Natives native, Args... args) {
    return NativePtr<Fn>(native)(args...);
}

// Raw helper for exploratory Lua/debug glue. Prefer typed CallNative<Fn>() in C++ code.
template <typename Ret = std::uintptr_t>
inline Ret CallNativeRaw(Natives native,
                         std::uintptr_t a0 = 0,
                         std::uintptr_t a1 = 0,
                         std::uintptr_t a2 = 0,
                         std::uintptr_t a3 = 0,
                         std::uintptr_t a4 = 0,
                         std::uintptr_t a5 = 0) {
    using Fn = Ret(__cdecl*)(std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
    return reinterpret_cast<Fn>(NativeAddress(native))(a0, a1, a2, a3, a4, a5);
}

} // namespace HookCrashers::NativeDB
