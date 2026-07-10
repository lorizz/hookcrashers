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
    CC_InitSaveManager = 0x48E60,
    CC_LoadSaveFile = 0x49812,
    CC_WriteSaveFile = 0x49F34,
    CC_CreateDefaultSave = 0x12F070,
    CC_WriteDefaultKeybindsToSave = 0x12EE30,
    CC_SanitizeCharacterSaveSlots = 0x12EB20,
    CC_GetCharacterLevelForPlayer = 0x579D6,
    CC_GetCharacterSkullCompletionForPlayer = 0x57A72,
    CC_IsBaseCharacterUnlockedForPlayer = 0x57A9E,
    CC_NormalizeCharacterSaveStats = 0x3974,
    CC_DebugUnlockCharactersForProfileTier = 0x3C81,
    CC_GetDlcEntitlementMask = 0x4A666,
    CC_RefreshDlcEntitlementsIntoSave = 0x496AA,
    CC_SetPainterFreshWorkshopUnlock = 0x4BC8,
    CC_SetFreshModeEnabled = 0x4A1BC,
    CC_LoadGlobalProfileFlagsFromSave = 0x49AFC,
    CC_ApplyDlcUnlockMaskToSave = 0x4CD2,
    CC_LoadKeybindsFromSave = 0x12F210,
    CC_ClearCharacterSyncPacket = 0x5FD40,
    CC_CopyIncomingCharacterSyncPayload = 0x5FE00,
    CC_CopySelectedIdsToPerPlayerWorkshopBlocks = 0x5FE70,
    CC_CopySelectedWorkshopIdsToPerPlayerBlocks = 0x5FF20,
    CC_MergeIncomingCharacterSyncPayload = 0x5FFD0,
    CC_MergeCharacterSyncPacketForRemoteLobby = 0x866E0,
    CC_ReconcileLobbyCharacterRuntimeEntries = 0x86D80,
    CC_ResolveCharacterSkinRuntime = 0x8D080,
    CC_ApplyCharacterSyncPacketToRoster = 0x87680,
    CC_BuildCharacterSyncPacket = 0x886D0,
    CC_SendInitialCharacterSyncPacket = 0xF20A0,
    CC_HandleIncomingCharacterSyncPacket = 0xF2310,
    CC_SetPendingWorkshopCloneSelection = 0xF2680,
    CC_RequestWorkshopCharacterCloneFromLobby = 0xF2770,
    CC_HandleWorkshopCloneButtonPressed = 0x160760,
    CC_CollectAvailableWorkshopCharacterIds = 0x891D0,
    CC_RequestRemoteWorkshopCharacterDownload = 0x89490,
    CC_ReportBlockedWorkshopCharacterInPacket = 0x897B0,
    CC_SyncCharacterListFromSave = 0xBC14E,
    CC_RebuildCharacterSlotTable = 0xBBDF8,
    CC_ValidateAddonCharacterQueryList = 0xBA264,
    CC_PruneInvalidWorkshopSkinSelections = 0xC66F8,
    CC_ValidateSessionWorkshopSkinsOrLeave = 0x5AE5A,
    CC_GameSWFNativeDispatcher = 0x124BBB,
    CC_RegisterGameSWFNativeFunctions = 0x124234,
    CC_RegisterAS2NativeFunctions = 0x10B1DA,
    CC_AS2NativeRegistryRegisterName = 0xABBDE,
    CC_RegisterLateGameSWFNativeGlobals = 0xFDEC,
    CC_SWFReadNextTagAndRemapCustomTags = 0x107348,
    CC_SWFHandleCustomBitmapTag148 = 0x10DECE,
    CC_SWFHandleCustomNoOpTag142 = 0x10E2A8,
    CC_SWFParseSpriteTagsUntilEnd = 0x10750E,
    CC_SWFParseFrameTagsBudgeted = 0x111456,
    CC_SWFRewriteTagHeaderForExporter = 0x10AE20,
    CC_SWFSceneParseHeader = 0x125440,
    CC_SWFNodeCtorLoadCOK6 = 0xFD7C0,
    CC_COK6ReadResourceMagic = 0x533B7,
    CC_ChangeLoadedMenuMovie = 0x37B20,
    CC_COK6MovieManagerUpdate = 0x128996,
    CC_RequestCOK6MovieTransition = 0x12A872,
    CC_COK6UpdateDefaultMenuVisibility = 0x128E1A,
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
    { Natives::CC_InitSaveManager, "CC_InitSaveManager", 0x48E60 },
    { Natives::CC_LoadSaveFile, "CC_LoadSaveFile", 0x49812 },
    { Natives::CC_WriteSaveFile, "CC_WriteSaveFile", 0x49F34 },
    { Natives::CC_CreateDefaultSave, "CC_CreateDefaultSave", 0x12F070 },
    { Natives::CC_WriteDefaultKeybindsToSave, "CC_WriteDefaultKeybindsToSave", 0x12EE30 },
    { Natives::CC_SanitizeCharacterSaveSlots, "CC_SanitizeCharacterSaveSlots", 0x12EB20 },
    { Natives::CC_GetCharacterLevelForPlayer, "CC_GetCharacterLevelForPlayer", 0x579D6 },
    { Natives::CC_GetCharacterSkullCompletionForPlayer, "CC_GetCharacterSkullCompletionForPlayer", 0x57A72 },
    { Natives::CC_IsBaseCharacterUnlockedForPlayer, "CC_IsBaseCharacterUnlockedForPlayer", 0x57A9E },
    { Natives::CC_NormalizeCharacterSaveStats, "CC_NormalizeCharacterSaveStats", 0x3974 },
    { Natives::CC_DebugUnlockCharactersForProfileTier, "CC_DebugUnlockCharactersForProfileTier", 0x3C81 },
    { Natives::CC_GetDlcEntitlementMask, "CC_GetDlcEntitlementMask", 0x4A666 },
    { Natives::CC_RefreshDlcEntitlementsIntoSave, "CC_RefreshDlcEntitlementsIntoSave", 0x496AA },
    { Natives::CC_SetPainterFreshWorkshopUnlock, "CC_SetPainterFreshWorkshopUnlock", 0x4BC8 },
    { Natives::CC_SetFreshModeEnabled, "CC_SetFreshModeEnabled", 0x4A1BC },
    { Natives::CC_LoadGlobalProfileFlagsFromSave, "CC_LoadGlobalProfileFlagsFromSave", 0x49AFC },
    { Natives::CC_ApplyDlcUnlockMaskToSave, "CC_ApplyDlcUnlockMaskToSave", 0x4CD2 },
    { Natives::CC_LoadKeybindsFromSave, "CC_LoadKeybindsFromSave", 0x12F210 },
    { Natives::CC_ClearCharacterSyncPacket, "CC_ClearCharacterSyncPacket", 0x5FD40 },
    { Natives::CC_CopyIncomingCharacterSyncPayload, "CC_CopyIncomingCharacterSyncPayload", 0x5FE00 },
    { Natives::CC_CopySelectedIdsToPerPlayerWorkshopBlocks, "CC_CopySelectedIdsToPerPlayerWorkshopBlocks", 0x5FE70 },
    { Natives::CC_CopySelectedWorkshopIdsToPerPlayerBlocks, "CC_CopySelectedWorkshopIdsToPerPlayerBlocks", 0x5FF20 },
    { Natives::CC_MergeIncomingCharacterSyncPayload, "CC_MergeIncomingCharacterSyncPayload", 0x5FFD0 },
    { Natives::CC_MergeCharacterSyncPacketForRemoteLobby, "CC_MergeCharacterSyncPacketForRemoteLobby", 0x866E0 },
    { Natives::CC_ReconcileLobbyCharacterRuntimeEntries, "CC_ReconcileLobbyCharacterRuntimeEntries", 0x86D80 },
    { Natives::CC_ResolveCharacterSkinRuntime, "CC_ResolveCharacterSkinRuntime", 0x8D080 },
    { Natives::CC_ApplyCharacterSyncPacketToRoster, "CC_ApplyCharacterSyncPacketToRoster", 0x87680 },
    { Natives::CC_BuildCharacterSyncPacket, "CC_BuildCharacterSyncPacket", 0x886D0 },
    { Natives::CC_SendInitialCharacterSyncPacket, "CC_SendInitialCharacterSyncPacket", 0xF20A0 },
    { Natives::CC_HandleIncomingCharacterSyncPacket, "CC_HandleIncomingCharacterSyncPacket", 0xF2310 },
    { Natives::CC_SetPendingWorkshopCloneSelection, "CC_SetPendingWorkshopCloneSelection", 0xF2680 },
    { Natives::CC_RequestWorkshopCharacterCloneFromLobby, "CC_RequestWorkshopCharacterCloneFromLobby", 0xF2770 },
    { Natives::CC_HandleWorkshopCloneButtonPressed, "CC_HandleWorkshopCloneButtonPressed", 0x160760 },
    { Natives::CC_CollectAvailableWorkshopCharacterIds, "CC_CollectAvailableWorkshopCharacterIds", 0x891D0 },
    { Natives::CC_RequestRemoteWorkshopCharacterDownload, "CC_RequestRemoteWorkshopCharacterDownload", 0x89490 },
    { Natives::CC_ReportBlockedWorkshopCharacterInPacket, "CC_ReportBlockedWorkshopCharacterInPacket", 0x897B0 },
    { Natives::CC_SyncCharacterListFromSave, "CC_SyncCharacterListFromSave", 0xBC14E },
    { Natives::CC_RebuildCharacterSlotTable, "CC_RebuildCharacterSlotTable", 0xBBDF8 },
    { Natives::CC_ValidateAddonCharacterQueryList, "CC_ValidateAddonCharacterQueryList", 0xBA264 },
    { Natives::CC_PruneInvalidWorkshopSkinSelections, "CC_PruneInvalidWorkshopSkinSelections", 0xC66F8 },
    { Natives::CC_ValidateSessionWorkshopSkinsOrLeave, "CC_ValidateSessionWorkshopSkinsOrLeave", 0x5AE5A },
    { Natives::CC_GameSWFNativeDispatcher, "CC_GameSWFNativeDispatcher", 0x124BBB },
    { Natives::CC_RegisterGameSWFNativeFunctions, "CC_RegisterGameSWFNativeFunctions", 0x124234 },
    { Natives::CC_RegisterAS2NativeFunctions, "CC_RegisterAS2NativeFunctions", 0x10B1DA },
    { Natives::CC_AS2NativeRegistryRegisterName, "CC_AS2NativeRegistryRegisterName", 0xABBDE },
    { Natives::CC_RegisterLateGameSWFNativeGlobals, "CC_RegisterLateGameSWFNativeGlobals", 0xFDEC },
    { Natives::CC_SWFReadNextTagAndRemapCustomTags, "CC_SWFReadNextTagAndRemapCustomTags", 0x107348 },
    { Natives::CC_SWFHandleCustomBitmapTag148, "CC_SWFHandleCustomBitmapTag148", 0x10DECE },
    { Natives::CC_SWFHandleCustomNoOpTag142, "CC_SWFHandleCustomNoOpTag142", 0x10E2A8 },
    { Natives::CC_SWFParseSpriteTagsUntilEnd, "CC_SWFParseSpriteTagsUntilEnd", 0x10750E },
    { Natives::CC_SWFParseFrameTagsBudgeted, "CC_SWFParseFrameTagsBudgeted", 0x111456 },
    { Natives::CC_SWFRewriteTagHeaderForExporter, "CC_SWFRewriteTagHeaderForExporter", 0x10AE20 },
    { Natives::CC_SWFSceneParseHeader, "CC_SWFSceneParseHeader", 0x125440 },
    { Natives::CC_SWFNodeCtorLoadCOK6, "CC_SWFNodeCtorLoadCOK6", 0xFD7C0 },
    { Natives::CC_COK6ReadResourceMagic, "CC_COK6ReadResourceMagic", 0x533B7 },
    { Natives::CC_ChangeLoadedMenuMovie, "CC_ChangeLoadedMenuMovie", 0x37B20 },
    { Natives::CC_COK6MovieManagerUpdate, "CC_COK6MovieManagerUpdate", 0x128996 },
    { Natives::CC_RequestCOK6MovieTransition, "CC_RequestCOK6MovieTransition", 0x12A872 },
    { Natives::CC_COK6UpdateDefaultMenuVisibility, "CC_COK6UpdateDefaultMenuVisibility", 0x128E1A },
    { Natives::CC_MenuActivateSubMenuState, "CC_MenuActivateSubMenuState", 0x29C4 },
    { Natives::CC_TextMenuItemSetLocalizedText, "CC_TextMenuItemSetLocalizedText", 0x2A4F8 },
    { Natives::CC_TextMenuItemBaseCtor, "CC_TextMenuItemBaseCtor", 0x2A19C },
    { Natives::CC_TextMenuItemCtor, "CC_TextMenuItemCtor", 0x2A228 },
    { Natives::CC_ButtonMenuItemBuildIcon, "CC_ButtonMenuItemBuildIcon", 0x2C5BE },
    { Natives::CC_DefaultMenuUpdateVisibleOptions, "CC_DefaultMenuUpdateVisibleOptions", 0x32E9C },
    { Natives::CC_MenuShowYesNoConfirmDialog, "CC_MenuShowYesNoConfirmDialog", 0x30A8 },
    { Natives::CC_MenuShowExitConfirmDialog, "CC_MenuShowExitConfirmDialog", 0x3266 }
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
