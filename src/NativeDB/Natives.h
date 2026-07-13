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

enum class Natives : std::uintptr_t {
    CC_InitSaveManager = 0x48EF0,
    CC_LoadSaveFile = 0x498A2,
    CC_WriteSaveFile = 0x49FC4,
    CC_CreateDefaultSave = 0x12F120,
    CC_WriteDefaultKeybindsToSave = 0x12EEE0,
    CC_SanitizeCharacterSaveSlots = 0x12EBD0,
    CC_GetCharacterLevelForPlayer = 0x57A66,
    CC_GetCharacterSkullCompletionForPlayer = 0x57B02,
    CC_IsBaseCharacterUnlockedForPlayer = 0x57B2E,
    CC_NormalizeCharacterSaveStats = 0x3974,
    CC_DebugUnlockCharactersForProfileTier = 0x3C81,
    CC_GetDlcEntitlementMask = 0x4A6F6,
    CC_RefreshDlcEntitlementsIntoSave = 0x4973A,
    CC_SetPainterFreshWorkshopUnlock = 0x4BC8,
    CC_SetFreshModeEnabled = 0x4A24C,
    CC_LoadGlobalProfileFlagsFromSave = 0x49B8C,
    CC_ApplyDlcUnlockMaskToSave = 0x4CD2,
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
    CC_SyncCharacterListFromSave = 0xBC1AE,
    CC_RebuildCharacterSlotTable = 0xBBE58,
    CC_ValidateAddonCharacterQueryList = 0xBA2C4,
    CC_PruneInvalidWorkshopSkinSelections = 0xC6758,
    CC_ValidateSessionWorkshopSkinsOrLeave = 0x5AEEA,
    CC_GameSWFNativeDispatcher = 0x124C6B,
    CC_RegisterGameSWFNativeFunctions = 0x1242E4,
    CC_RegisterAS2NativeFunctions = 0x10B26A,
    CC_AS2NativeRegistryRegisterName = 0xABC4E,
    CC_RegisterLateGameSWFNativeGlobals = 0xFDEC,
    CC_SWFReadNextTagAndRemapCustomTags = 0x1073C8,
    CC_SWFHandleCustomBitmapTag148 = 0x10DF7E,
    CC_SWFHandleCustomNoOpTag142 = 0x10E358,
    CC_SWFParseSpriteTagsUntilEnd = 0x10758E,
    CC_SWFParseFrameTagsBudgeted = 0x111506,
    CC_SWFRewriteTagHeaderForExporter = 0x10AEB0,
    CC_SWFSceneParseHeader = 0x1254F0,
    CC_SWFNodeCtorLoadCOK6 = 0xFD840,
    CC_COK6ReadResourceMagic = 0x53447,
    CC_ChangeLoadedMenuMovie = 0x37BC0,
    CC_COK6MovieManagerUpdate = 0x128A46,
    CC_RequestCOK6MovieTransition = 0x12A922,
    CC_COK6UpdateDefaultMenuVisibility = 0x128ECA,
    CC_MenuActivateSubMenuState = 0x29C4,
    CC_TextMenuItemSetLocalizedText = 0x2A4F8,
    CC_TextMenuItemBaseCtor = 0x2A19C,
    CC_TextMenuItemCtor = 0x2A228,
    CC_ButtonMenuItemBuildIcon = 0x2C5BE,
    CC_DefaultMenuUpdateVisibleOptions = 0x32E9C,
    CC_MenuShowYesNoConfirmDialog = 0x30A8,
    CC_MenuShowExitConfirmDialog = 0x3266
};

struct NativeInfo {
    Natives id;
    const char* name;
    std::uintptr_t rva;
};

inline constexpr NativeInfo kNativeInfos[] = {
#define HC_NATIVE_INFO(name) { Natives::name, #name, static_cast<std::uintptr_t>(Natives::name) }
    HC_NATIVE_INFO(CC_InitSaveManager),
    HC_NATIVE_INFO(CC_LoadSaveFile),
    HC_NATIVE_INFO(CC_WriteSaveFile),
    HC_NATIVE_INFO(CC_CreateDefaultSave),
    HC_NATIVE_INFO(CC_WriteDefaultKeybindsToSave),
    HC_NATIVE_INFO(CC_SanitizeCharacterSaveSlots),
    HC_NATIVE_INFO(CC_GetCharacterLevelForPlayer),
    HC_NATIVE_INFO(CC_GetCharacterSkullCompletionForPlayer),
    HC_NATIVE_INFO(CC_IsBaseCharacterUnlockedForPlayer),
    HC_NATIVE_INFO(CC_NormalizeCharacterSaveStats),
    HC_NATIVE_INFO(CC_DebugUnlockCharactersForProfileTier),
    HC_NATIVE_INFO(CC_GetDlcEntitlementMask),
    HC_NATIVE_INFO(CC_RefreshDlcEntitlementsIntoSave),
    HC_NATIVE_INFO(CC_SetPainterFreshWorkshopUnlock),
    HC_NATIVE_INFO(CC_SetFreshModeEnabled),
    HC_NATIVE_INFO(CC_LoadGlobalProfileFlagsFromSave),
    HC_NATIVE_INFO(CC_ApplyDlcUnlockMaskToSave),
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
    HC_NATIVE_INFO(CC_RebuildCharacterSlotTable),
    HC_NATIVE_INFO(CC_ValidateAddonCharacterQueryList),
    HC_NATIVE_INFO(CC_PruneInvalidWorkshopSkinSelections),
    HC_NATIVE_INFO(CC_ValidateSessionWorkshopSkinsOrLeave),
    HC_NATIVE_INFO(CC_GameSWFNativeDispatcher),
    HC_NATIVE_INFO(CC_RegisterGameSWFNativeFunctions),
    HC_NATIVE_INFO(CC_RegisterAS2NativeFunctions),
    HC_NATIVE_INFO(CC_AS2NativeRegistryRegisterName),
    HC_NATIVE_INFO(CC_RegisterLateGameSWFNativeGlobals),
    HC_NATIVE_INFO(CC_SWFReadNextTagAndRemapCustomTags),
    HC_NATIVE_INFO(CC_SWFHandleCustomBitmapTag148),
    HC_NATIVE_INFO(CC_SWFHandleCustomNoOpTag142),
    HC_NATIVE_INFO(CC_SWFParseSpriteTagsUntilEnd),
    HC_NATIVE_INFO(CC_SWFParseFrameTagsBudgeted),
    HC_NATIVE_INFO(CC_SWFRewriteTagHeaderForExporter),
    HC_NATIVE_INFO(CC_SWFSceneParseHeader),
    HC_NATIVE_INFO(CC_SWFNodeCtorLoadCOK6),
    HC_NATIVE_INFO(CC_COK6ReadResourceMagic),
    HC_NATIVE_INFO(CC_ChangeLoadedMenuMovie),
    HC_NATIVE_INFO(CC_COK6MovieManagerUpdate),
    HC_NATIVE_INFO(CC_RequestCOK6MovieTransition),
    HC_NATIVE_INFO(CC_COK6UpdateDefaultMenuVisibility),
    HC_NATIVE_INFO(CC_MenuActivateSubMenuState),
    HC_NATIVE_INFO(CC_TextMenuItemSetLocalizedText),
    HC_NATIVE_INFO(CC_TextMenuItemBaseCtor),
    HC_NATIVE_INFO(CC_TextMenuItemCtor),
    HC_NATIVE_INFO(CC_ButtonMenuItemBuildIcon),
    HC_NATIVE_INFO(CC_DefaultMenuUpdateVisibleOptions),
    HC_NATIVE_INFO(CC_MenuShowYesNoConfirmDialog),
    HC_NATIVE_INFO(CC_MenuShowExitConfirmDialog)
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
