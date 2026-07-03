#include "MenuOnlineCompatibilityHook.h"

#include "OnlineCompatibilityMode.h"
#include "../Util/Logger.h"
#include <spdlog/logger.h>
#include "../../include/HookCrashers/Public/Globals.h"

#include <windows.h>
#include <detours.h>
#include <cstdint>
#include <cstring>
#include <new>
#include <sstream>

namespace HookCrashers::Save {
namespace {
    constexpr uintptr_t kBuildOnlineMenuRva = 0x15AC70;
    constexpr uintptr_t kSetMenuStateRva = 0x12C410;
    constexpr uintptr_t kBootstrapAllocatorRva = 0x0F69B0;
    constexpr uintptr_t kInitAllocatorRva = 0x0F6B20;
    constexpr uintptr_t kGetAllocatorRva = 0x0F6A40;
    constexpr uintptr_t kAllocateRva = 0x0DFE10;
    constexpr uintptr_t kPrepareStringObjectRva = 0x02EE80;
    constexpr uintptr_t kGraphicMenuItemRva = 0x155620;
    constexpr uintptr_t kFinalizeAndAddElementRva = 0x152340;
    constexpr uintptr_t kGlobalDisplayConfigRva = 0x391294;
    constexpr uintptr_t kAllocatorReadyByteRva = 0x360EE1;
    constexpr uintptr_t kCriticalSectionRva = 0x361118;

    constexpr int kPlayWithFriendsAssetId = 10000;
    constexpr int kGraphicMenuItemSize = 0x5C;
    constexpr size_t kMenuHandlerSize = 0x20C;
    constexpr size_t kOriginalMenuHandlerCount = 46;
    constexpr size_t kExpandedMenuHandlerCount = 47;
    constexpr uint16_t kPlayWithFriendsBuilderMenuId = 37;
    constexpr uint16_t kPlayWithFriendsMenuId = 37;
    constexpr uint16_t kModeMenuId = 36;
    constexpr uint16_t kOnlineCompatibilityMenuId = 46;
    constexpr size_t kCurrentHandlerOffset = 56;
    constexpr size_t kHandlerTableOffset = 64;
    constexpr size_t kMenuItemCountOffset = 0x04;
    constexpr size_t kGraphicCountOffset = 0x06;
    constexpr size_t kButtonCountOffset = 0x08;
    constexpr size_t kHotspotCountOffset = 0x0A;
    constexpr size_t kHotspotArrayOffset = 0x1C;
    constexpr size_t kMenuSelectionOffset = 0x0C;
    constexpr size_t kMenuObjectArrayOffset = 0x44;
    constexpr size_t kGraphicObjectArrayOffset = 0x144;
    constexpr size_t kGraphicMenuItemDisplayObjectOffset = 0x54;
    constexpr size_t kHotspotSelectOffset = 0x20;
    constexpr size_t kHotspotClickOffset = 0x28;
    constexpr size_t kMenuUpdateCallbackOffset = 0x1C8;
    constexpr uintptr_t kGotoAndStopRva = 0x11A250;

    using BuildOnlineMenuFn = int(__thiscall*)(uint32_t* self, int menuState);
    using SetMenuStateFn = int(__thiscall*)(uint32_t* self, int16_t menuId, int menuState);
    using BootstrapAllocatorFn = void*(__cdecl*)();
    using InitAllocatorFn = void(__thiscall*)(void* allocator);
    using GetAllocatorFn = void*(__cdecl*)();
    using AllocateFn = void*(__thiscall*)(void* allocator, int size);
    using PrepareStringObjectFn = void*(__thiscall*)(void* stringObject, const char* value);
    using GraphicMenuItemFn = int(__thiscall*)(void* storage, uint32_t scene, void* exportName, int assetId, float x, float y);
    using FinalizeAndAddElementFn = unsigned int(__thiscall*)(int menuState, void* object);
    using GotoAndStopFn = int(__thiscall*)(void* clip, int frame);

    BuildOnlineMenuFn g_originalBuildOnlineMenu = nullptr;
    SetMenuStateFn g_originalSetMenuState = nullptr;
    BootstrapAllocatorFn g_bootstrapAllocator = nullptr;
    InitAllocatorFn g_initAllocator = nullptr;
    GetAllocatorFn g_getAllocator = nullptr;
    AllocateFn g_allocate = nullptr;
    PrepareStringObjectFn g_prepareStringObject = nullptr;
    GraphicMenuItemFn g_graphicMenuItem = nullptr;
    FinalizeAndAddElementFn g_finalizeAndAddElement = nullptr;
    GotoAndStopFn g_gotoAndStop = nullptr;

    uint32_t* g_expandedOwner = nullptr;
    char* g_expandedHandlers = nullptr;
    uintptr_t g_originalHandlers = 0;
    bool g_onlineCompatibilityHandlerBuilt = false;
    bool g_onlineCompatibilityKeyboardArmed = false;
    bool g_previousOnlineUp = false;
    bool g_previousOnlineDown = false;
    bool g_previousOnlineEnter = false;
    bool g_previousOnlineEscape = false;

    void LogLine(const std::string& message);
    std::string Hex(uintptr_t value);
    void ResetOnlineCompatibilityKeyboardState() {
        g_onlineCompatibilityKeyboardArmed = false;
        g_previousOnlineUp = false;
        g_previousOnlineDown = false;
        g_previousOnlineEnter = false;
        g_previousOnlineEscape = false;
    }
    void SetOnlineCompatibilityFrame(uintptr_t menuState, uint16_t selectedIndex) {
        if (!menuState || !g_gotoAndStop) {
            return;
        }

        uintptr_t graphicMenuItem = *reinterpret_cast<uintptr_t*>(menuState + kGraphicObjectArrayOffset);
        if (!graphicMenuItem) {
            graphicMenuItem = *reinterpret_cast<uintptr_t*>(menuState + 0x148);
        }
        if (!graphicMenuItem) {
            return;
        }

        void* displayObject = *reinterpret_cast<void**>(graphicMenuItem + kGraphicMenuItemDisplayObjectOffset);
        if (!displayObject) {
            return;
        }

        g_gotoAndStop(displayObject, static_cast<int>(selectedIndex + 1));
    }

    int SetOnlineCompatibilitySelection(int self, uint16_t selectedIndex) {
        if (!self) {
            return 0;
        }

        uintptr_t menuState = *reinterpret_cast<uintptr_t*>(static_cast<uintptr_t>(self) + kCurrentHandlerOffset);
        if (!menuState) {
            return self;
        }

        const uint16_t itemCount = *reinterpret_cast<uint16_t*>(menuState + kMenuItemCountOffset);
        auto* menuItems = reinterpret_cast<uintptr_t*>(menuState + kMenuObjectArrayOffset);
        for (uint16_t i = 0; i < itemCount; ++i) {
            uintptr_t item = menuItems[i];
            if (!item) {
                continue;
            }

            uint32_t color = 0x777777FF;
            const uint32_t customColor = *reinterpret_cast<uint32_t*>(item + 0x14);
            if (customColor) {
                color = customColor;
            }
            *reinterpret_cast<uint32_t*>(item + 0x10) = color;
        }

        *reinterpret_cast<uint16_t*>(menuState + kMenuSelectionOffset) = selectedIndex;
        if (selectedIndex < itemCount && menuItems[selectedIndex]) {
            *reinterpret_cast<uint32_t*>(menuItems[selectedIndex] + 0x10) = 0xFFFF00FF;
        }

        SetOnlineCompatibilityFrame(menuState, selectedIndex);
        return self;
    }

    int __cdecl SelectPlayVanilla(int self) {
        return SetOnlineCompatibilitySelection(self, 0);
    }

    int __cdecl SelectPlayModded(int self) {
        return SetOnlineCompatibilitySelection(self, 1);
    }
    int OpenPlayWithFriendsWithMode(uint32_t* self, OnlineCompatibilityMode mode) {
        SetOnlineCompatibilityMode(mode);
        uintptr_t handlerTable = self ? *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(self) + kHandlerTableOffset) : 0;
        if (!handlerTable) {
            return 0;
        }

        int playWithFriendsState = static_cast<int>(handlerTable + kPlayWithFriendsBuilderMenuId * kMenuHandlerSize);
        std::ostringstream stream;
        stream << "[OnlineCompatibility] Select " << OnlineCompatibilityModeName(mode)
               << " -> SetMenuState " << kPlayWithFriendsBuilderMenuId
               << " state=" << Hex(static_cast<uintptr_t>(playWithFriendsState)) << ".";
        LogLine(stream.str());
        return g_originalSetMenuState(self, static_cast<int16_t>(kPlayWithFriendsBuilderMenuId), playWithFriendsState);
    }

    int __cdecl PlayVanillaClicked(int self) {
        return OpenPlayWithFriendsWithMode(reinterpret_cast<uint32_t*>(self), OnlineCompatibilityMode::Vanilla);
    }

    int __cdecl PlayModdedClicked(int self) {
        return OpenPlayWithFriendsWithMode(reinterpret_cast<uint32_t*>(self), OnlineCompatibilityMode::Modded);
    }
    int __cdecl OnlineCompatibilityUpdate(int self) {
        if (!self) {
            return 0;
        }

        uintptr_t menuState = *reinterpret_cast<uintptr_t*>(static_cast<uintptr_t>(self) + kCurrentHandlerOffset);
        if (!menuState) {
            return self;
        }

        uint16_t selectedIndex = *reinterpret_cast<uint16_t*>(menuState + kMenuSelectionOffset);
        if (selectedIndex > 1) {
            selectedIndex = 0;
        }

        const bool upDown = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
        const bool downDown = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
        const bool enterDown = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
        const bool escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;

        if (!g_onlineCompatibilityKeyboardArmed) {
            if (!upDown && !downDown && !enterDown && !escapeDown) {
                g_onlineCompatibilityKeyboardArmed = true;
            }
            g_previousOnlineUp = upDown;
            g_previousOnlineDown = downDown;
            g_previousOnlineEnter = enterDown;
            g_previousOnlineEscape = escapeDown;
            return self;
        }

        const bool movePressed = (upDown && !g_previousOnlineUp) || (downDown && !g_previousOnlineDown);
        const bool enterPressed = enterDown && !g_previousOnlineEnter;
        const bool escapePressed = escapeDown && !g_previousOnlineEscape;

        g_previousOnlineUp = upDown;
        g_previousOnlineDown = downDown;
        g_previousOnlineEnter = enterDown;
        g_previousOnlineEscape = escapeDown;

        if (movePressed) {
            selectedIndex = selectedIndex == 0 ? 1 : 0;
            SetOnlineCompatibilitySelection(self, selectedIndex);
            return self;
        }

        if (enterPressed) {
            if (selectedIndex == 0) {
                return PlayVanillaClicked(self);
            }
            return PlayModdedClicked(self);
        }

        if (escapePressed) {
            uintptr_t handlerTable = *reinterpret_cast<uintptr_t*>(static_cast<uintptr_t>(self) + kHandlerTableOffset);
            if (handlerTable && g_originalSetMenuState) {
                SetOnlineCompatibilityMode(OnlineCompatibilityMode::Unknown);
                int modeMenuState = static_cast<int>(handlerTable + kModeMenuId * kMenuHandlerSize);
                return g_originalSetMenuState(reinterpret_cast<uint32_t*>(self), static_cast<int16_t>(kModeMenuId), modeMenuState);
            }
        }

        return self;
    }

    void LogLine(const std::string& message) {
        auto* logger = HookCrashers::Util::Logger::Instance().Get();
        if (logger) {
            logger->info("{}", message);
        }
    }

    std::string Hex(uintptr_t value) {
        std::ostringstream stream;
        stream << "0x" << std::hex << std::uppercase << value;
        return stream.str();
    }

    uint32_t ReadU32(uintptr_t address) {
        return *reinterpret_cast<uint32_t*>(address);
    }

    void EnsureGameAllocatorReady() {
        auto* ready = reinterpret_cast<uint8_t*>(g_moduleBase + kAllocatorReadyByteRva);
        if (*ready) {
            return;
        }

        auto* criticalSection = reinterpret_cast<LPCRITICAL_SECTION>(g_moduleBase + kCriticalSectionRva);
        InitializeCriticalSection(criticalSection);
        void* allocator = g_bootstrapAllocator();
        g_initAllocator(allocator);
    }

    void* AllocateGameObject(int size) {
        EnsureGameAllocatorReady();

        auto* criticalSection = reinterpret_cast<LPCRITICAL_SECTION>(g_moduleBase + kCriticalSectionRva);
        EnterCriticalSection(criticalSection);
        void* allocator = g_getAllocator();
        void* storage = allocator ? g_allocate(allocator, size) : nullptr;
        LeaveCriticalSection(criticalSection);
        return storage;
    }

    bool TryCreateOnlineCompatibilityObject(void* storage, uint32_t scene, void* exportName, int menuState, float x, float y, int* object) {
        __try {
            *object = g_graphicMenuItem(storage, scene, exportName, kPlayWithFriendsAssetId, x, y);
            g_finalizeAndAddElement(menuState, reinterpret_cast<void*>(*object));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool CreateOnlineCompatibilityClip(uint32_t* self, int menuState, int* outObject) {
        if (outObject) {
            *outObject = 0;
        }
        if (!self || !menuState) {
            return false;
        }

        void* storage = AllocateGameObject(kGraphicMenuItemSize);
        if (!storage) {
            LogLine("[OnlineCompatibility] Failed to allocate onlinecompatibility GraphicMenuItem storage.");
            return false;
        }

        uint32_t exportName[22]{};
        g_prepareStringObject(exportName, "onlinecompatibility");

        const uintptr_t displayConfig = ReadU32(g_moduleBase + kGlobalDisplayConfigRva);
        const float x = displayConfig ? static_cast<float>(ReadU32(displayConfig + 12)) * 0.5f : 0.0f;
        const float y = displayConfig ? static_cast<float>(ReadU32(displayConfig + 16)) * 0.5f : 0.0f;
        const uint32_t scene = self[1];

        int object = 0;
        if (!TryCreateOnlineCompatibilityObject(storage, scene, exportName, menuState, x, y, &object)) {
            LogLine("[OnlineCompatibility] Exception while creating onlinecompatibility GraphicMenuItem.");
            return false;
        }
        if (outObject) {
            *outObject = object;
        }

        std::ostringstream stream;
        stream << "[OnlineCompatibility] Created onlinecompatibility GraphicMenuItem storage=" << Hex(reinterpret_cast<uintptr_t>(storage))
               << " object=" << Hex(static_cast<uintptr_t>(object))
               << " menu_state=" << Hex(static_cast<uintptr_t>(menuState))
               << " scene=" << Hex(static_cast<uintptr_t>(scene))
               << " x=" << x
               << " y=" << y << ".";
        LogLine(stream.str());
        return object != 0;
    }

    bool EnsureOnlineCompatibilityHandler(uint32_t* self, int originalMenuState) {
        if (!self || !originalMenuState) {
            return false;
        }

        auto* ownerBytes = reinterpret_cast<uint8_t*>(self);
        auto* handlerTablePtr = reinterpret_cast<uintptr_t*>(ownerBytes + kHandlerTableOffset);
        uintptr_t currentHandlers = *handlerTablePtr;
        if (!currentHandlers) {
            LogLine("[OnlineCompatibility] Cannot expand menu handlers: handler table is null.");
            return false;
        }

        if (g_expandedOwner != self || g_originalHandlers != currentHandlers && currentHandlers != reinterpret_cast<uintptr_t>(g_expandedHandlers)) {
            g_expandedOwner = self;
            g_originalHandlers = currentHandlers;
            g_onlineCompatibilityHandlerBuilt = false;
            g_expandedHandlers = new (std::nothrow) char[kExpandedMenuHandlerCount * kMenuHandlerSize];
            if (!g_expandedHandlers) {
                LogLine("[OnlineCompatibility] Cannot expand menu handlers: allocation failed.");
                return false;
            }

            std::memcpy(g_expandedHandlers, reinterpret_cast<void*>(currentHandlers), kOriginalMenuHandlerCount * kMenuHandlerSize);
            *handlerTablePtr = reinterpret_cast<uintptr_t>(g_expandedHandlers);

            auto* currentHandlerPtr = reinterpret_cast<uintptr_t*>(ownerBytes + kCurrentHandlerOffset);
            if (*currentHandlerPtr >= currentHandlers && *currentHandlerPtr < currentHandlers + kOriginalMenuHandlerCount * kMenuHandlerSize) {
                *currentHandlerPtr = reinterpret_cast<uintptr_t>(g_expandedHandlers) + (*currentHandlerPtr - currentHandlers);
            }

            std::ostringstream stream;
            stream << "[OnlineCompatibility] Expanded menu handler table old=" << Hex(currentHandlers)
                   << " new=" << Hex(reinterpret_cast<uintptr_t>(g_expandedHandlers))
                   << " count=" << kExpandedMenuHandlerCount << ".";
            LogLine(stream.str());
        }

        auto* handlers = reinterpret_cast<uint8_t*>(g_expandedHandlers);
        auto* playWithFriendsHandler = handlers + kPlayWithFriendsBuilderMenuId * kMenuHandlerSize;
        auto* compatibilityHandler = handlers + kOnlineCompatibilityMenuId * kMenuHandlerSize;

        if (!g_onlineCompatibilityHandlerBuilt) {
            std::memcpy(compatibilityHandler, playWithFriendsHandler, kMenuHandlerSize);
            uint16_t copiedHotspotCount = *reinterpret_cast<uint16_t*>(compatibilityHandler + kHotspotCountOffset);
            *reinterpret_cast<uint16_t*>(compatibilityHandler + kMenuItemCountOffset) = 0;
            *reinterpret_cast<uint16_t*>(compatibilityHandler + kGraphicCountOffset) = 0;
            *reinterpret_cast<uint16_t*>(compatibilityHandler + kHotspotCountOffset) = copiedHotspotCount;
            int object = 0;
            g_onlineCompatibilityHandlerBuilt = CreateOnlineCompatibilityClip(self, reinterpret_cast<int>(compatibilityHandler), &object);
            std::ostringstream stream;
            uint16_t hotspotCount = *reinterpret_cast<uint16_t*>(compatibilityHandler + kHotspotCountOffset);
            auto* hotspots = reinterpret_cast<uintptr_t*>(compatibilityHandler + kHotspotArrayOffset);
            if (hotspotCount >= 1 && hotspots[0]) {
                *reinterpret_cast<uintptr_t*>(hotspots[0] + kHotspotSelectOffset) = reinterpret_cast<uintptr_t>(&SelectPlayVanilla);
                *reinterpret_cast<uintptr_t*>(hotspots[0] + kHotspotClickOffset) = reinterpret_cast<uintptr_t>(&PlayVanillaClicked);
            }
            if (hotspotCount >= 2 && hotspots[1]) {
                *reinterpret_cast<uintptr_t*>(hotspots[1] + kHotspotSelectOffset) = reinterpret_cast<uintptr_t>(&SelectPlayModded);
                *reinterpret_cast<uintptr_t*>(hotspots[1] + kHotspotClickOffset) = reinterpret_cast<uintptr_t>(&PlayModdedClicked);
            }
            *reinterpret_cast<uint16_t*>(compatibilityHandler + kMenuSelectionOffset) = 0;
            *reinterpret_cast<uintptr_t*>(compatibilityHandler + kMenuUpdateCallbackOffset) = reinterpret_cast<uintptr_t>(&OnlineCompatibilityUpdate);
            ResetOnlineCompatibilityKeyboardState();
            SetOnlineCompatibilityFrame(reinterpret_cast<uintptr_t>(compatibilityHandler), 0);

            stream << "[OnlineCompatibility] Built compatibility handler id=" << kOnlineCompatibilityMenuId
                   << " handler=" << Hex(reinterpret_cast<uintptr_t>(compatibilityHandler))
                   << " object=" << Hex(static_cast<uintptr_t>(object))
                   << " copied_from=" << kPlayWithFriendsBuilderMenuId
                   << " hotspots=" << hotspotCount << ".";
            LogLine(stream.str());
        }

        return g_onlineCompatibilityHandlerBuilt;
    }

    int __fastcall DetouredSetMenuState(uint32_t* self, void*, int16_t menuId, int menuState) {
        auto* ownerBytes = reinterpret_cast<uint8_t*>(self);
        uintptr_t handlerTable = self ? *reinterpret_cast<uintptr_t*>(ownerBytes + kHandlerTableOffset) : 0;
        uintptr_t currentHandler = self ? *reinterpret_cast<uintptr_t*>(ownerBytes + kCurrentHandlerOffset) : 0;

        if (menuId == 23 || menuId == 36 || menuId == 37 || menuId == kOnlineCompatibilityMenuId) {
            std::ostringstream seen;
            seen << "[OnlineCompatibility] SetMenuState seen id=" << menuId
                 << " state=" << Hex(static_cast<uintptr_t>(menuState))
                 << " table=" << Hex(handlerTable)
                 << " expanded=" << Hex(reinterpret_cast<uintptr_t>(g_expandedHandlers))
                 << " current=" << Hex(currentHandler)
                 << " ready=" << (g_onlineCompatibilityHandlerBuilt ? "true" : "false")
                 << " mode=" << OnlineCompatibilityModeName(GetOnlineCompatibilityMode()) << ".";
            LogLine(seen.str());
        }

        if (menuId == kPlayWithFriendsMenuId && GetOnlineCompatibilityMode() == OnlineCompatibilityMode::Unknown) {
            if (handlerTable && handlerTable == reinterpret_cast<uintptr_t>(g_expandedHandlers) && g_onlineCompatibilityHandlerBuilt) {
                int redirectedState = static_cast<int>(handlerTable + kOnlineCompatibilityMenuId * kMenuHandlerSize);
                std::ostringstream stream;
                stream << "[OnlineCompatibility] Redirect SetMenuState " << kPlayWithFriendsMenuId
                       << " -> " << kOnlineCompatibilityMenuId
                       << " state=" << Hex(static_cast<uintptr_t>(redirectedState)) << ".";
                LogLine(stream.str());
                ResetOnlineCompatibilityKeyboardState();
                return g_originalSetMenuState(self, static_cast<int16_t>(kOnlineCompatibilityMenuId), redirectedState);
            }
        }

        return g_originalSetMenuState(self, menuId, menuState);
    }

    int __fastcall DetouredBuildOnlineMenu(uint32_t* self, void*, int menuState) {
        SetOnlineCompatibilityMode(OnlineCompatibilityMode::Unknown);

        std::ostringstream enter;
        enter << "[OnlineCompatibility] BuildPlayWithFriendsMenu enter self=" << Hex(reinterpret_cast<uintptr_t>(self))
              << " menu_state=" << Hex(static_cast<uintptr_t>(menuState))
              << " rva=" << Hex(kBuildOnlineMenuRva) << ".";
        LogLine(enter.str());

        const int result = g_originalBuildOnlineMenu(self, menuState);
        const bool handlerReady = EnsureOnlineCompatibilityHandler(self, menuState);

        std::ostringstream leave;
        leave << "[OnlineCompatibility] BuildPlayWithFriendsMenu leave result=" << Hex(static_cast<uintptr_t>(result))
              << " handler_ready=" << (handlerReady ? "true" : "false")
              << " mode=" << OnlineCompatibilityModeName(GetOnlineCompatibilityMode()) << ".";
        LogLine(leave.str());
        return result;
    }
}

bool SetupMenuOnlineCompatibilityHook() {
    g_originalBuildOnlineMenu = reinterpret_cast<BuildOnlineMenuFn>(g_moduleBase + kBuildOnlineMenuRva);
    g_originalSetMenuState = reinterpret_cast<SetMenuStateFn>(g_moduleBase + kSetMenuStateRva);
    g_bootstrapAllocator = reinterpret_cast<BootstrapAllocatorFn>(g_moduleBase + kBootstrapAllocatorRva);
    g_initAllocator = reinterpret_cast<InitAllocatorFn>(g_moduleBase + kInitAllocatorRva);
    g_getAllocator = reinterpret_cast<GetAllocatorFn>(g_moduleBase + kGetAllocatorRva);
    g_allocate = reinterpret_cast<AllocateFn>(g_moduleBase + kAllocateRva);
    g_prepareStringObject = reinterpret_cast<PrepareStringObjectFn>(g_moduleBase + kPrepareStringObjectRva);
    g_graphicMenuItem = reinterpret_cast<GraphicMenuItemFn>(g_moduleBase + kGraphicMenuItemRva);
    g_finalizeAndAddElement = reinterpret_cast<FinalizeAndAddElementFn>(g_moduleBase + kFinalizeAndAddElementRva);
    g_gotoAndStop = reinterpret_cast<GotoAndStopFn>(g_moduleBase + kGotoAndStopRva);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG attachResult = DetourAttach(&(PVOID&)g_originalBuildOnlineMenu, DetouredBuildOnlineMenu);
    if (attachResult == NO_ERROR) {
        attachResult = DetourAttach(&(PVOID&)g_originalSetMenuState, DetouredSetMenuState);
    }
    if (attachResult != NO_ERROR) {
        OutputDebugStringA("[OnlineCompatibility] BuildPlayWithFriendsMenu DetourAttach failed.\n");
        DetourTransactionAbort();
        return false;
    }

    LONG commitResult = DetourTransactionCommit();
    if (commitResult != NO_ERROR) {
        OutputDebugStringA("[OnlineCompatibility] BuildPlayWithFriendsMenu DetourTransactionCommit failed.\n");
        return false;
    }

    std::ostringstream stream;
    stream << "[OnlineCompatibility] Hook installed name=BuildPlayWithFriendsMenu rva=" << Hex(kBuildOnlineMenuRva)
           << " set_menu=" << Hex(kSetMenuStateRva)
           << " graphic=" << Hex(kGraphicMenuItemRva)
           << " finalize=" << Hex(kFinalizeAndAddElementRva) << ".";
    LogLine(stream.str());
    return true;
}

} // namespace HookCrashers::Save
















