#include "MenuOnlineCompatibilityHook.h"

#include "OnlineCompatibilityMode.h"
#include "../Util/Logger.h"
#include "../Util/MemoryPatcher.h"
#include "../SWF/Runtime/SWFRuntimeInjector.h"
#include "../Game/MenuStructs.h"
#include <spdlog/logger.h>
#include "../../include/HookCrashers/Public/Globals.h"

#include <windows.h>
#include <detours.h>
#include <cstdint>
#include <cstring>
#include <new>
#include <sstream>
#include <intrin.h>
#include <vector>

namespace HookCrashers::Save {
namespace {
    constexpr uintptr_t kMenuBuilderRva = 0x144270;
    constexpr uintptr_t kBuildOnlineMenuRva = 0x15AC70;
    constexpr uintptr_t kSetMenuStateRva = 0x12C410;
    constexpr uintptr_t kBootstrapAllocatorRva = 0x0F69B0;
    constexpr uintptr_t kInitAllocatorRva = 0x0F6B20;
    constexpr uintptr_t kGetAllocatorRva = 0x0F6A40;
    constexpr uintptr_t kAllocateRva = 0x0DFE10;
    constexpr uintptr_t kPrepareStringObjectRva = 0x02EE80;
    constexpr uintptr_t kGraphicMenuItemRva = 0x155620;
    constexpr uintptr_t kInitGraphicMenuItemRva = 0x155550;
    constexpr uintptr_t kButtonMenuItemSetPresetRva = 0x156270;
    constexpr uintptr_t kFinalizeAndAddElementRva = 0x152340;
    constexpr uintptr_t kMenuConfirmSoundRva = 0x0C9D80;
    constexpr uintptr_t kStartMenuTransitionRva = 0x0C3160;
    constexpr uintptr_t kApplyPendingMenuStateRva = 0x150E50;
    constexpr uintptr_t kStartPlayWithFriendsTransitionRva = 0x15B940;
    constexpr uintptr_t kGlobalDisplayConfigRva = 0x391294;
    constexpr uintptr_t kAllocatorReadyByteRva = 0x360EE1;
    constexpr uintptr_t kCriticalSectionRva = 0x361118;

    constexpr int kPlayWithFriendsAssetId = 10000;
    constexpr int kGraphicMenuItemSize = 0x5C;
    constexpr int kButtonMenuItemSize = 0x60;
    constexpr uintptr_t kButtonMenuItemVtableRva = 0x1D0AE0;
    constexpr size_t kMenuHandlerSize = 0x20C;
    constexpr size_t kOriginalMenuHandlerCount = 46;
    constexpr size_t kExpandedMenuHandlerCount = 48;
    constexpr uint16_t kPlayWithFriendsBuilderMenuId = 37;
    constexpr uint16_t kPlayWithFriendsMenuId = 37;
    constexpr uint16_t kModeMenuId = 36;
    constexpr uint16_t kOnlineLobbyMenuId = 39;
    constexpr uint16_t kOnlineCompatibilityMenuId = 46;
    constexpr uint16_t kPlayerLobbiesMenuId = 47;
    constexpr size_t kCurrentHandlerOffset = 56;
    constexpr size_t kHandlerTableOffset = 64;
    constexpr size_t kMenuItemCountOffset = 0x04;
    constexpr size_t kGraphicCountOffset = 0x06;
    constexpr size_t kButtonCountOffset = 0x08;
    constexpr size_t kHotspotCountOffset = 0x0A;
    constexpr size_t kHotspotArrayOffset = 0x1C;
    constexpr size_t kMenuSelectionOffset = 0x0C;
    constexpr size_t kMenuObjectArrayOffset = 0x44;
    constexpr size_t kButtonObjectArrayOffset = 0x184;
    constexpr size_t kGraphicObjectArrayOffset = 0x144;
    constexpr size_t kGraphicMenuItemDisplayObjectOffset = 0x54;
    constexpr size_t kHotspotSelectOffset = 0x20;
    constexpr size_t kHotspotClickOffset = 0x28;
    constexpr size_t kHotspotObjectSize = 0x34;
    constexpr size_t kMenuOwnerOffset = 0x1AC;
    constexpr size_t kMenuBackCallbackOffset = 0x1B4;
    constexpr size_t kMenuUpdateCallbackOffset = 0x1C8;
    constexpr size_t kMenuInitCallbackOffset = 0x1D0;
    constexpr size_t kMenuConfiguredTargetOffset = 0x1DA;
    constexpr uintptr_t kGotoAndStopRva = 0x11A250;
    constexpr uintptr_t kConstructMenuArrayRva = 0x165E49;
    constexpr uintptr_t kMenuConstructorRva = 0x152F40;
    constexpr uintptr_t kMenuDestructorRva = 0x0A8F30;
    constexpr uintptr_t kOpenOnlineModeMenuCallerRva = 0x14A4CD;

    using MenuBuilderFn = int(__thiscall*)(uint32_t* self, int param2, int param3);
    using BuildOnlineMenuFn = int(__thiscall*)(uint32_t* self, int menuState);
    using SetMenuStateFn = int(__thiscall*)(uint32_t* self, int16_t menuId, int menuState);
    using BootstrapAllocatorFn = void*(__cdecl*)();
    using InitAllocatorFn = void(__thiscall*)(void* allocator);
    using GetAllocatorFn = void*(__cdecl*)();
    using AllocateFn = void*(__thiscall*)(void* allocator, int size);
    using PrepareStringObjectFn = void*(__thiscall*)(void* stringObject, const char* value);
    using GraphicMenuItemFn = int(__thiscall*)(void* storage, uint32_t scene, void* exportName, int assetId, float x, float y);
    using InitGraphicMenuItemFn = int(__thiscall*)(void* storage, uint32_t scene);
    using ButtonMenuItemSetPresetFn = void(__thiscall*)(void* button, void* context);
    using FinalizeAndAddElementFn = unsigned int(__thiscall*)(int menuState, void* object);
    using GotoAndStopFn = int(__thiscall*)(void* clip, int frame);
    using MenuConfirmSoundFn = uintptr_t;
    using StartMenuTransitionFn = uint8_t(__thiscall*)(uint32_t* self, uint8_t fadeIn, uint8_t longDelay, uint8_t unknown, uint8_t fromLateMenu);
    using StartPlayWithFriendsTransitionFn = int(__cdecl*)(int self);
    using ConstructMenuArrayFn = void(__cdecl*)(void* base, int itemSize, int count, void* constructor, void* destructor);

    MenuBuilderFn g_originalMenuBuilder = nullptr;
    BuildOnlineMenuFn g_originalBuildOnlineMenu = nullptr;
    SetMenuStateFn g_originalSetMenuState = nullptr;
    BootstrapAllocatorFn g_bootstrapAllocator = nullptr;
    InitAllocatorFn g_initAllocator = nullptr;
    GetAllocatorFn g_getAllocator = nullptr;
    AllocateFn g_allocate = nullptr;
    PrepareStringObjectFn g_prepareStringObject = nullptr;
    GraphicMenuItemFn g_graphicMenuItem = nullptr;
    InitGraphicMenuItemFn g_initGraphicMenuItem = nullptr;
    ButtonMenuItemSetPresetFn g_buttonMenuItemSetPreset = nullptr;
    FinalizeAndAddElementFn g_finalizeAndAddElement = nullptr;
    GotoAndStopFn g_gotoAndStop = nullptr;
    MenuConfirmSoundFn g_menuConfirmSound = 0;
    StartMenuTransitionFn g_startMenuTransition = nullptr;
    StartPlayWithFriendsTransitionFn g_startPlayWithFriendsTransition = nullptr;
    ConstructMenuArrayFn g_constructMenuArray = nullptr;

    uint32_t* g_expandedOwner = nullptr;
    char* g_expandedHandlers = nullptr;
    uintptr_t g_originalHandlers = 0;
    bool g_onlineCompatibilityHandlerBuilt = false;
    uintptr_t g_onlineCompatibilityGraphicObject = 0;
    bool g_onlineCompatibilityKeyboardArmed = false;
    bool g_previousOnlineUp = false;
    bool g_previousOnlineDown = false;
    bool g_previousOnlineEnter = false;
    bool g_previousOnlineEscape = false;
    bool g_suppressNextPlayWithFriendsLobbyOpen = false;
    bool g_returnPlayWithFriendsToCompatibility = false;
    bool g_suppressNextModeMenuBack = false;
    bool g_onlineModeMenuArmed = false;
    bool g_playerLobbiesHandlerBuilt = false;
    int g_playerLobbiesSelectedIndex = -1;
    bool g_playerLobbiesKeyboardArmed = false;
    bool g_previousPlayerLobbiesUp = false;
    bool g_previousPlayerLobbiesDown = false;
    bool g_previousPlayerLobbiesEscape = false;


    void LogLine(const std::string& message);
    std::string Hex(uintptr_t value);
    int __cdecl PlayerLobbiesUpdate(int self);
    bool EnsureOnlineCompatibilityHandler(uint32_t* self, int originalMenuState);
    bool EnsurePlayerLobbiesHandler(uint32_t* self);
    int __fastcall DetouredSetMenuState(uint32_t* self, void*, int16_t menuId, int menuState);
    uint32_t ReadU32(uintptr_t address);
    void ResetOnlineCompatibilityKeyboardState() {
        g_onlineCompatibilityKeyboardArmed = false;
        g_previousOnlineUp = false;
        g_previousOnlineDown = false;
        g_previousOnlineEnter = false;
        g_previousOnlineEscape = false;
    }

    void ResetPlayerLobbiesSelectionState() {
        g_playerLobbiesSelectedIndex = -1;
        g_playerLobbiesKeyboardArmed = false;
        g_previousPlayerLobbiesUp = false;
        g_previousPlayerLobbiesDown = false;
        g_previousPlayerLobbiesEscape = false;
    }

    bool TryGetClientMouse(float& x, float& y, uint32_t& width, uint32_t& height) {
        HWND window = GetForegroundWindow();
        if (!window) {
            return false;
        }

        POINT point{};
        if (!GetCursorPos(&point) || !ScreenToClient(window, &point)) {
            return false;
        }

        RECT rect{};
        if (!GetClientRect(window, &rect)) {
            return false;
        }

        width = static_cast<uint32_t>(rect.right - rect.left);
        height = static_cast<uint32_t>(rect.bottom - rect.top);
        if (!width || !height) {
            return false;
        }

        x = static_cast<float>(point.x);
        y = static_cast<float>(point.y);
        return true;
    }

    int GetHoveredPlayerLobbyRow() {
        float mouseX = 0.0f;
        float mouseY = 0.0f;
        uint32_t clientWidth = 0;
        uint32_t clientHeight = 0;
        if (!TryGetClientMouse(mouseX, mouseY, clientWidth, clientHeight)) {
            return -1;
        }

        const uintptr_t displayConfig = ReadU32(g_moduleBase + kGlobalDisplayConfigRva);
        const float designWidth = displayConfig ? static_cast<float>(ReadU32(displayConfig + 12)) : 800.0f;
        const float designHeight = displayConfig ? static_cast<float>(ReadU32(displayConfig + 16)) : 600.0f;
        if (designWidth <= 0.0f || designHeight <= 0.0f) {
            return -1;
        }

        const float x = mouseX * (designWidth / static_cast<float>(clientWidth));
        const float y = mouseY * (designHeight / static_cast<float>(clientHeight));
        const float centerX = designWidth * 0.5f;
        const float centerY = designHeight * 0.5f;
        constexpr float kRowLocalY[4] = { -154.85f, -85.10f, -15.65f, 54.00f };
        constexpr float kRowHalfHeight = 31.0f;
        constexpr float kRowLeftOffset = -285.0f;
        constexpr float kRowRightOffset = 285.0f;

        if (x < centerX + kRowLeftOffset || x > centerX + kRowRightOffset) {
            return -1;
        }
        for (int i = 0; i < 4; ++i) {
            const float rowY = centerY + kRowLocalY[i];
            if (y >= rowY - kRowHalfHeight && y <= rowY + kRowHalfHeight) {
                return i;
            }
        }
        return -1;
    }

    void PlayMenuConfirmSound() {
        if (!g_menuConfirmSound) {
            return;
        }
        const uintptr_t soundFn = g_menuConfirmSound;
        __asm {
            xorps xmm2, xmm2
            xorps xmm3, xmm3
            push 0
            push 1
            call soundFn
            add esp, 8
        }
    }
    void SetOnlineCompatibilityFrame(uintptr_t menuState, uint16_t selectedIndex) {
        if (!menuState || !g_gotoAndStop) {
            return;
        }

        uintptr_t graphicMenuItem = g_onlineCompatibilityGraphicObject;
        if (!graphicMenuItem) {
            LogLine("[OnlineCompatibility] Skip frame change: onlinecompatibility graphic object is null.");
            return;
        }

        void* displayObject = *reinterpret_cast<void**>(graphicMenuItem + kGraphicMenuItemDisplayObjectOffset);
        if (!displayObject) {
            std::ostringstream stream;
            stream << "[OnlineCompatibility] Skip frame change: graphic=" << Hex(graphicMenuItem)
                   << " display_object=null.";
            LogLine(stream.str());
            return;
        }

        std::ostringstream stream;
        stream << "[OnlineCompatibility] Set onlinecompatibility frame graphic=" << Hex(graphicMenuItem)
               << " display=" << Hex(reinterpret_cast<uintptr_t>(displayObject))
               << " frame=" << (selectedIndex + 1) << ".";
        LogLine(stream.str());
        g_gotoAndStop(displayObject, static_cast<int>(selectedIndex + 1));
    }

    void HideOnlineCompatibilityGraphic() {
        const uintptr_t graphicMenuItem = g_onlineCompatibilityGraphicObject;
        if (!graphicMenuItem) {
            return;
        }

        uintptr_t displayObject = *reinterpret_cast<uintptr_t*>(graphicMenuItem + kGraphicMenuItemDisplayObjectOffset);
        if (!displayObject) {
            return;
        }

        *reinterpret_cast<uint8_t*>(displayObject + 0xEC) &= ~0x80u;
        LogLine("[OnlineCompatibility] Hid onlinecompatibility graphic before opening playwithfriends.");
    }

    void DetachOnlineCompatibilitySharedObjects(uintptr_t handlerTable) {
        if (!handlerTable) {
            return;
        }

        HideOnlineCompatibilityGraphic();

        auto* compatibilityHandler = reinterpret_cast<uint8_t*>(handlerTable + kOnlineCompatibilityMenuId * kMenuHandlerSize);
        *reinterpret_cast<uint16_t*>(compatibilityHandler + kMenuItemCountOffset) = 0;
        *reinterpret_cast<uint16_t*>(compatibilityHandler + kGraphicCountOffset) = 0;
        *reinterpret_cast<uint16_t*>(compatibilityHandler + kButtonCountOffset) = 0;
        *reinterpret_cast<uint16_t*>(compatibilityHandler + kHotspotCountOffset) = 0;
        std::memset(compatibilityHandler + kHotspotArrayOffset, 0, 10 * sizeof(uintptr_t));
        std::memset(compatibilityHandler + kMenuObjectArrayOffset, 0, 64 * sizeof(uintptr_t));
        std::memset(compatibilityHandler + kGraphicObjectArrayOffset, 0, 16 * sizeof(uintptr_t));
        std::memset(compatibilityHandler + kButtonObjectArrayOffset, 0, 8 * sizeof(uintptr_t));

        g_onlineCompatibilityGraphicObject = 0;
        g_onlineCompatibilityHandlerBuilt = false;
        LogLine("[OnlineCompatibility] Detached all clone arrays before opening playwithfriends.");
    }
    bool TryHideGraphicDisplay(uintptr_t graphicMenuItem) {
        if (!graphicMenuItem) {
            return false;
        }

        __try {
            uintptr_t displayObject = *reinterpret_cast<uintptr_t*>(graphicMenuItem + kGraphicMenuItemDisplayObjectOffset);
            if (!displayObject) {
                return false;
            }
            *reinterpret_cast<uint8_t*>(displayObject + 0xEC) &= ~0x80u;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool TryDetachMenuHandlerArrays(uintptr_t menuState) {
        if (!menuState) {
            return false;
        }

        __try {
            uint16_t graphicCount = *reinterpret_cast<uint16_t*>(menuState + kGraphicCountOffset);
            if (graphicCount > 16) {
                graphicCount = 16;
            }

            auto* graphics = reinterpret_cast<uintptr_t*>(menuState + kGraphicObjectArrayOffset);
            for (uint16_t i = 0; i < graphicCount; ++i) {
                TryHideGraphicDisplay(graphics[i]);
            }

            *reinterpret_cast<uint16_t*>(menuState + kMenuItemCountOffset) = 0;
            *reinterpret_cast<uint16_t*>(menuState + kGraphicCountOffset) = 0;
            *reinterpret_cast<uint16_t*>(menuState + kButtonCountOffset) = 0;
            *reinterpret_cast<uint16_t*>(menuState + kHotspotCountOffset) = 0;
            std::memset(reinterpret_cast<void*>(menuState + kHotspotArrayOffset), 0, 10 * sizeof(uintptr_t));
            std::memset(reinterpret_cast<void*>(menuState + kMenuObjectArrayOffset), 0, 64 * sizeof(uintptr_t));
            std::memset(reinterpret_cast<void*>(menuState + kGraphicObjectArrayOffset), 0, 16 * sizeof(uintptr_t));
            std::memset(reinterpret_cast<void*>(menuState + kButtonObjectArrayOffset), 0, 8 * sizeof(uintptr_t));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    void DetachMenuHandlerArraysForTransition(uintptr_t menuState, const char* reason) {
        if (!TryDetachMenuHandlerArrays(menuState)) {
            LogLine("[OnlineCompatibility] Failed to detach menu handler arrays before transition.");
            return;
        }

        std::ostringstream stream;
        stream << "[OnlineCompatibility] Detached current handler arrays before " << reason
               << " state=" << Hex(menuState) << ".";
        LogLine(stream.str());
    }
    void DetachOnlineCompatibilityForTransition(uint32_t* self, const char* reason) {
        if (!self) {
            return;
        }

        auto* mainMenu = reinterpret_cast<uint8_t*>(self);
        uintptr_t handlerTable = *reinterpret_cast<uintptr_t*>(mainMenu + kHandlerTableOffset);
        uintptr_t currentState = *reinterpret_cast<uintptr_t*>(mainMenu + kCurrentHandlerOffset);
        const uintptr_t compatibilityState = handlerTable ? handlerTable + kOnlineCompatibilityMenuId * kMenuHandlerSize : 0;
        if (!compatibilityState || currentState != compatibilityState) {
            return;
        }

        DetachMenuHandlerArraysForTransition(compatibilityState, reason ? reason : "menu transition");
        g_onlineCompatibilityHandlerBuilt = false;
        g_onlineCompatibilityGraphicObject = 0;
        ResetOnlineCompatibilityKeyboardState();
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
        SetOnlineCompatibilityMode(mode == OnlineCompatibilityMode::Vanilla ? OnlineCompatibilityMode::Unknown : mode);
        if (!self || !g_startPlayWithFriendsTransition) {
            return 0;
        }

        auto* mainMenu = reinterpret_cast<uint8_t*>(self);
        uintptr_t handlerTable = *reinterpret_cast<uintptr_t*>(mainMenu + kHandlerTableOffset);
        const uintptr_t playWithFriendsState = handlerTable ? handlerTable + kPlayWithFriendsBuilderMenuId * kMenuHandlerSize : 0;
        uintptr_t currentState = *reinterpret_cast<uintptr_t*>(mainMenu + kCurrentHandlerOffset);
        if (currentState == handlerTable + kOnlineCompatibilityMenuId * kMenuHandlerSize) {
            *reinterpret_cast<uint16_t*>(currentState + kMenuSelectionOffset) = 0;
            *reinterpret_cast<uint16_t*>(mainMenu + 12) = 0;
        }
        HideOnlineCompatibilityGraphic();
        ResetOnlineCompatibilityKeyboardState();
        std::ostringstream stream;
        stream << "[OnlineCompatibility] Select " << OnlineCompatibilityModeName(mode)
               << " -> vanilla MainMenu_StartPlayWithFriendsTransition"
               << " self=" << Hex(reinterpret_cast<uintptr_t>(self))
               << " current=" << Hex(*reinterpret_cast<uintptr_t*>(mainMenu + kCurrentHandlerOffset))
               << " active_id=" << *reinterpret_cast<uint16_t*>(mainMenu + 60)
               << " transition_obj=" << Hex(*reinterpret_cast<uintptr_t*>(mainMenu + 0x20C))
               << " playwithfriends=" << Hex(playWithFriendsState) << ".";
        LogLine(stream.str());
        g_suppressNextPlayWithFriendsLobbyOpen = false;
        g_returnPlayWithFriendsToCompatibility = mode == OnlineCompatibilityMode::Vanilla;
        return g_startPlayWithFriendsTransition(reinterpret_cast<int>(self));
    }
    int __cdecl PlayVanillaClicked(int self) {
        return OpenPlayWithFriendsWithMode(reinterpret_cast<uint32_t*>(self), OnlineCompatibilityMode::Vanilla);
    }

    int __cdecl PlayModdedClicked(int self) {
        auto* menu = reinterpret_cast<uint32_t*>(self);
        if (!menu || !g_originalSetMenuState) {
            return 0;
        }
        SetOnlineCompatibilityMode(OnlineCompatibilityMode::Modded);
        uintptr_t handlerTable = *reinterpret_cast<uintptr_t*>(static_cast<uintptr_t>(self) + kHandlerTableOffset);
        if (!handlerTable) {
            return self;
        }
        if (!EnsurePlayerLobbiesHandler(menu)) {
            LogLine("[OnlineCompatibility] Failed to build playerlobbies handler.");
            return self;
        }
        const int playerLobbiesState = static_cast<int>(handlerTable + kPlayerLobbiesMenuId * kMenuHandlerSize);
        DetachOnlineCompatibilityForTransition(menu, "opening playerlobbies");
        std::ostringstream stream;
        stream << "[OnlineCompatibility] Select modded -> SetMenuState playerlobbies id=" << kPlayerLobbiesMenuId
               << " state=" << Hex(static_cast<uintptr_t>(playerLobbiesState)) << ".";
        LogLine(stream.str());
        return g_originalSetMenuState(menu, static_cast<int16_t>(kPlayerLobbiesMenuId), playerLobbiesState);
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
                g_suppressNextModeMenuBack = true;
                int modeMenuState = static_cast<int>(handlerTable + kModeMenuId * kMenuHandlerSize);
                return g_originalSetMenuState(reinterpret_cast<uint32_t*>(self), static_cast<int16_t>(kModeMenuId), modeMenuState);
            }
        }

        return self;
    }

    int __cdecl PlayerLobbiesUpdate(int self) {
        if (!self) {
            return 0;
        }

        const bool upDown = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
        const bool downDown = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
        const bool escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;

        if (!g_playerLobbiesKeyboardArmed) {
            if (!upDown && !downDown && !escapeDown) {
                g_playerLobbiesKeyboardArmed = true;
            }
            g_previousPlayerLobbiesUp = upDown;
            g_previousPlayerLobbiesDown = downDown;
            g_previousPlayerLobbiesEscape = escapeDown;
            return self;
        }

        const bool upPressed = upDown && !g_previousPlayerLobbiesUp;
        const bool downPressed = downDown && !g_previousPlayerLobbiesDown;
        const bool escapePressed = escapeDown && !g_previousPlayerLobbiesEscape;

        g_previousPlayerLobbiesUp = upDown;
        g_previousPlayerLobbiesDown = downDown;
        g_previousPlayerLobbiesEscape = escapeDown;

        const int hoveredRow = GetHoveredPlayerLobbyRow();
        if (hoveredRow >= 0 && hoveredRow != g_playerLobbiesSelectedIndex) {
            g_playerLobbiesSelectedIndex = hoveredRow;
        }

        if (downPressed) {
            g_playerLobbiesSelectedIndex = g_playerLobbiesSelectedIndex < 0 ? 0 : (g_playerLobbiesSelectedIndex + 1) & 3;
        }
        else if (upPressed) {
            g_playerLobbiesSelectedIndex = g_playerLobbiesSelectedIndex < 0 ? 3 : (g_playerLobbiesSelectedIndex + 3) & 3;
        }

        if (escapePressed) {
            uintptr_t handlerTable = *reinterpret_cast<uintptr_t*>(static_cast<uintptr_t>(self) + kHandlerTableOffset);
            if (handlerTable && g_originalSetMenuState) {
                uintptr_t playerLobbiesState = handlerTable + kPlayerLobbiesMenuId * kMenuHandlerSize;
                DetachMenuHandlerArraysForTransition(playerLobbiesState, "closing playerlobbies");
                g_playerLobbiesHandlerBuilt = false;
                ResetPlayerLobbiesSelectionState();
                EnsureOnlineCompatibilityHandler(reinterpret_cast<uint32_t*>(self), static_cast<int>(handlerTable + kOnlineCompatibilityMenuId * kMenuHandlerSize));
                ResetOnlineCompatibilityKeyboardState();
                int compatibilityState = static_cast<int>(handlerTable + kOnlineCompatibilityMenuId * kMenuHandlerSize);
                return g_originalSetMenuState(reinterpret_cast<uint32_t*>(self), static_cast<int16_t>(kOnlineCompatibilityMenuId), compatibilityState);
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

    bool TryCreateGraphicMenuItemObject(void* storage, uint32_t scene, void* exportName, int assetId, float x, float y, int* object) {
        __try {
            *object = g_graphicMenuItem(storage, scene, exportName, assetId, x, y);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }


    void CloneOnlineCompatibilityHotspots(uint8_t* compatibilityHandler) {
        if (!compatibilityHandler) {
            return;
        }

        uint16_t hotspotCount = *reinterpret_cast<uint16_t*>(compatibilityHandler + kHotspotCountOffset);
        auto* hotspots = reinterpret_cast<uintptr_t*>(compatibilityHandler + kHotspotArrayOffset);
        for (uint16_t i = 0; i < hotspotCount; ++i) {
            if (!hotspots[i]) {
                continue;
            }

            void* clonedHotspot = AllocateGameObject(static_cast<int>(kHotspotObjectSize));
            if (!clonedHotspot) {
                LogLine("[OnlineCompatibility] Failed to clone hotspot object.");
                continue;
            }

            std::memcpy(clonedHotspot, reinterpret_cast<void*>(hotspots[i]), kHotspotObjectSize);
            hotspots[i] = reinterpret_cast<uintptr_t>(clonedHotspot);
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
        if (!TryCreateGraphicMenuItemObject(storage, scene, exportName, kPlayWithFriendsAssetId, x, y, &object)) {
            LogLine("[OnlineCompatibility] Exception while creating onlinecompatibility GraphicMenuItem.");
            return false;
        }
        if (outObject) {
            *outObject = object;
        }

        std::ostringstream stream;
        stream << "[OnlineCompatibility] Created replacement onlinecompatibility GraphicMenuItem storage=" << Hex(reinterpret_cast<uintptr_t>(storage))
               << " object=" << Hex(static_cast<uintptr_t>(object))
               << " menu_state=" << Hex(static_cast<uintptr_t>(menuState))
               << " scene=" << Hex(static_cast<uintptr_t>(scene))
               << " x=" << x
               << " y=" << y << ".";
        LogLine(stream.str());
        return object != 0;
    }


    HookCrashers::Game::MenuState* GetMenu(uint32_t* self, uint16_t menuId) {
        if (!self) {
            return nullptr;
        }
        auto* mainMenu = reinterpret_cast<HookCrashers::Game::MainMenu*>(self);
        if (!mainMenu->menuTable) {
            return nullptr;
        }
        return mainMenu->menuTable + menuId;
    }

    void CreateMenu(HookCrashers::Game::MenuState* menu, const HookCrashers::Game::MenuState* templateMenu) {
        if (!menu || !templateMenu) {
            return;
        }
        const void* menuVtable = templateMenu->vtable;
        std::memset(menu, 0, sizeof(HookCrashers::Game::MenuState));
        menu->vtable = const_cast<void*>(menuVtable);
        menu->selectedIndex = 0;
        menu->field_14 = 1;
    }

    HookCrashers::Game::GraphicMenuItem* CreateGraphicMenuItem(
        uint32_t* self,
        HookCrashers::Game::MenuState* menu,
        const char* exportNameText,
        int assetId,
        float x,
        float y,
        bool finalize) {
        if (!self || !menu || !exportNameText) {
            return nullptr;
        }

        void* storage = AllocateGameObject(sizeof(HookCrashers::Game::GraphicMenuItem));
        if (!storage) {
            return nullptr;
        }

        uint32_t exportName[22]{};
        g_prepareStringObject(exportName, exportNameText);
        int object = 0;
        if (!TryCreateGraphicMenuItemObject(storage, self[1], exportName, assetId, x, y, &object)) {
            return nullptr;
        }
        if (finalize && object) {
            g_finalizeAndAddElement(reinterpret_cast<int>(menu), reinterpret_cast<void*>(object));
        }
        return reinterpret_cast<HookCrashers::Game::GraphicMenuItem*>(object);
    }

    int CreateGraphicMenuItem(uint32_t* self, int menuState, const char* exportNameText, int assetId, float x, float y, bool finalize) {
        return reinterpret_cast<int>(CreateGraphicMenuItem(
            self,
            reinterpret_cast<HookCrashers::Game::MenuState*>(menuState),
            exportNameText,
            assetId,
            x,
            y,
            finalize));
    }

    HookCrashers::Game::ButtonMenuItem* CreateButtonMenuItem(uint32_t* self, HookCrashers::Game::MenuState* menu, uint32_t presetId) {
        if (!self || !menu || !g_initGraphicMenuItem || !g_buttonMenuItemSetPreset) {
            return nullptr;
        }

        void* storage = AllocateGameObject(kButtonMenuItemSize);
        if (!storage) {
            return nullptr;
        }

        g_initGraphicMenuItem(storage, self[1]);
        *reinterpret_cast<uintptr_t*>(storage) = g_moduleBase + kButtonMenuItemVtableRva;
        *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(storage) + 0x5C) = presetId;
        g_buttonMenuItemSetPreset(storage, nullptr);

        const uint32_t fallbackColor = *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(storage) + 0x14);
        *reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(storage) + 0x10) = fallbackColor ? fallbackColor : 0xAAAAAAFF;
        *reinterpret_cast<uintptr_t*>(storage) = g_moduleBase + kButtonMenuItemVtableRva;

        g_finalizeAndAddElement(reinterpret_cast<int>(menu), storage);
        return reinterpret_cast<HookCrashers::Game::ButtonMenuItem*>(storage);
    }

    HookCrashers::Game::ButtonMenuItem* CreateBackButton(uint32_t* self, HookCrashers::Game::MenuState* menu) {
        return CreateButtonMenuItem(self, menu, 0);
    }

    void AddOnlineCompatibilityHotspot(
        HookCrashers::Game::MenuState* menu,
        uint32_t* self,
        float x1,
        float y1,
        float x2,
        float y2,
        uintptr_t onSelect,
        uintptr_t onClick) {
        if (!menu || !self || menu->hotspotCount >= (sizeof(menu->hotspots) / sizeof(menu->hotspots[0]))) {
            return;
        }

        auto* hotspot = reinterpret_cast<HookCrashers::Game::MenuHotspot*>(AllocateGameObject(sizeof(HookCrashers::Game::MenuHotspot)));
        if (!hotspot) {
            return;
        }
        std::memset(hotspot, 0, sizeof(HookCrashers::Game::MenuHotspot));
        hotspot->left = x1;
        hotspot->top = y1;
        hotspot->right = x2;
        hotspot->bottom = y2;
        hotspot->flags |= 1;
        hotspot->owner = self;
        hotspot->onSelect = reinterpret_cast<void*>(onSelect);
        hotspot->onClick = reinterpret_cast<void*>(onClick);
        menu->hotspots[menu->hotspotCount++] = hotspot;
    }

    bool BuildOnlineCompatibilityHandlerFromScratch(
        uint32_t* self,
        HookCrashers::Game::MenuState* compatibilityHandler,
        const HookCrashers::Game::MenuState* templateHandler) {
        if (!self || !compatibilityHandler || !templateHandler) {
            return false;
        }

        CreateMenu(compatibilityHandler, templateHandler);

        const uintptr_t displayConfig = ReadU32(g_moduleBase + kGlobalDisplayConfigRva);
        const uint32_t width = displayConfig ? ReadU32(displayConfig + 12) : 0;
        const uint32_t height = displayConfig ? ReadU32(displayConfig + 16) : 0;
        const float centerX = static_cast<float>(width) * 0.5f;
        const float centerY = static_cast<float>(height) * 0.5f;
        const float modeMenuY = height == 576 ? 68.0f : 0.0f;

        auto* modeMenuObject = CreateGraphicMenuItem(self, compatibilityHandler, "modemenu", 2006, centerX, modeMenuY, true);
        auto* onlineObject = CreateGraphicMenuItem(self, compatibilityHandler, "onlinecompatibility", kPlayWithFriendsAssetId, centerX, centerY, true);
        g_onlineCompatibilityGraphicObject = reinterpret_cast<uintptr_t>(onlineObject);
        auto* backButton = CreateBackButton(self, compatibilityHandler);

        std::ostringstream buildStream;
        buildStream << "[OnlineCompatibility] Scratch visuals modemenu=" << Hex(reinterpret_cast<uintptr_t>(modeMenuObject))
                    << " online=" << Hex(reinterpret_cast<uintptr_t>(onlineObject))
                    << " back=" << Hex(reinterpret_cast<uintptr_t>(backButton))
                    << " counts menu=" << compatibilityHandler->menuItemCount
                    << " graphic=" << compatibilityHandler->graphicCount
                    << " button=" << compatibilityHandler->buttonCount << ".";
        LogLine(buildStream.str());

        AddOnlineCompatibilityHotspot(compatibilityHandler, self, 289.0f, 147.5f, 539.0f, 202.5f, reinterpret_cast<uintptr_t>(&SelectPlayVanilla), reinterpret_cast<uintptr_t>(&PlayVanillaClicked));
        AddOnlineCompatibilityHotspot(compatibilityHandler, self, 289.0f, 212.5f, 539.0f, 267.5f, reinterpret_cast<uintptr_t>(&SelectPlayModded), reinterpret_cast<uintptr_t>(&PlayModdedClicked));

        compatibilityHandler->owner = self;
        compatibilityHandler->updateCallback = reinterpret_cast<void*>(&OnlineCompatibilityUpdate);
        compatibilityHandler->initCallback = nullptr;
        compatibilityHandler->configuredTargetMenuId = kModeMenuId;
        ResetOnlineCompatibilityKeyboardState();
        SetOnlineCompatibilityFrame(reinterpret_cast<uintptr_t>(compatibilityHandler), 0);
        return onlineObject != nullptr;
    }

    bool EnsureOnlineCompatibilityHandler(uint32_t* self, int originalMenuState) {
        if (!self || !originalMenuState) {
            return false;
        }

        auto* mainMenu = reinterpret_cast<HookCrashers::Game::MainMenu*>(self);
        uintptr_t handlerTable = reinterpret_cast<uintptr_t>(mainMenu->menuTable);
        if (!handlerTable) {
            LogLine("[OnlineCompatibility] Cannot build menu handler: handler table is null.");
            return false;
        }

        if (g_expandedOwner != self || g_originalHandlers != handlerTable) {
            g_expandedOwner = self;
            g_originalHandlers = handlerTable;
            g_expandedHandlers = reinterpret_cast<char*>(handlerTable);
            g_onlineCompatibilityHandlerBuilt = false;
            g_onlineCompatibilityGraphicObject = 0;
        }

        auto* playWithFriendsHandler = GetMenu(self, kPlayWithFriendsBuilderMenuId);
        auto* compatibilityHandler = GetMenu(self, kOnlineCompatibilityMenuId);

        if (!g_onlineCompatibilityHandlerBuilt) {
            const bool built = BuildOnlineCompatibilityHandlerFromScratch(self, compatibilityHandler, playWithFriendsHandler);
            g_onlineCompatibilityHandlerBuilt = built;

            std::ostringstream stream;
            stream << "[OnlineCompatibility] Built registered onlinecompatibility menu id=" << kOnlineCompatibilityMenuId
                   << " handler=" << Hex(reinterpret_cast<uintptr_t>(compatibilityHandler))
                   << " table=" << Hex(handlerTable)
                   << " menu_items=" << (compatibilityHandler ? compatibilityHandler->menuItemCount : 0)
                   << " graphics=" << (compatibilityHandler ? compatibilityHandler->graphicCount : 0)
                   << " buttons=" << (compatibilityHandler ? compatibilityHandler->buttonCount : 0)
                   << " hotspots=" << (compatibilityHandler ? compatibilityHandler->hotspotCount : 0)
                   << " update=" << Hex(compatibilityHandler ? reinterpret_cast<uintptr_t>(compatibilityHandler->updateCallback) : 0)
                   << " init=" << Hex(compatibilityHandler ? reinterpret_cast<uintptr_t>(compatibilityHandler->initCallback) : 0) << ".";
            LogLine(stream.str());
        }
        return g_onlineCompatibilityHandlerBuilt;
    }
    bool BuildPlayerLobbiesHandlerFromScratch(uint32_t* self, uint8_t* playerLobbiesHandler, uint8_t* templateHandler) {
        if (!self || !playerLobbiesHandler || !templateHandler) {
            return false;
        }

        const uintptr_t menuVtable = *reinterpret_cast<uintptr_t*>(templateHandler);
        std::memset(playerLobbiesHandler, 0, kMenuHandlerSize);
        *reinterpret_cast<uintptr_t*>(playerLobbiesHandler) = menuVtable;
        *reinterpret_cast<uint16_t*>(playerLobbiesHandler + kMenuItemCountOffset) = 0;
        *reinterpret_cast<uint16_t*>(playerLobbiesHandler + kGraphicCountOffset) = 0;
        *reinterpret_cast<uint16_t*>(playerLobbiesHandler + kButtonCountOffset) = 0;
        *reinterpret_cast<uint16_t*>(playerLobbiesHandler + kHotspotCountOffset) = 0;
        *reinterpret_cast<uint16_t*>(playerLobbiesHandler + kMenuSelectionOffset) = 0;
        *reinterpret_cast<uint8_t*>(playerLobbiesHandler + 0x14) = 1;

        const uintptr_t displayConfig = ReadU32(g_moduleBase + kGlobalDisplayConfigRva);
        const uint32_t width = displayConfig ? ReadU32(displayConfig + 12) : 0;
        const uint32_t height = displayConfig ? ReadU32(displayConfig + 16) : 0;
        const float centerX = static_cast<float>(width) * 0.5f;
        const float centerY = static_cast<float>(height) * 0.5f;

        const uint16_t playerLobbiesAssetId = HookCrashers::SWF::Runtime::GetInjectedMatchmakingMenuSpriteId();
        const int lobbyObject = CreateGraphicMenuItem(
            self,
            reinterpret_cast<int>(playerLobbiesHandler),
            "playerlobbies",
            playerLobbiesAssetId ? playerLobbiesAssetId : kPlayWithFriendsAssetId,
            centerX,
            centerY,
            true);
        const uintptr_t lobbyDisplay = lobbyObject
            ? *reinterpret_cast<uintptr_t*>(static_cast<uintptr_t>(lobbyObject) + kGraphicMenuItemDisplayObjectOffset)
            : 0;
        *reinterpret_cast<uintptr_t*>(playerLobbiesHandler + kMenuOwnerOffset) = reinterpret_cast<uintptr_t>(self);
        *reinterpret_cast<uintptr_t*>(playerLobbiesHandler + kMenuUpdateCallbackOffset) = reinterpret_cast<uintptr_t>(&PlayerLobbiesUpdate);
        *reinterpret_cast<uintptr_t*>(playerLobbiesHandler + kMenuInitCallbackOffset) = 0;
        *reinterpret_cast<uint16_t*>(playerLobbiesHandler + kMenuConfiguredTargetOffset) = kOnlineCompatibilityMenuId;
        ResetPlayerLobbiesSelectionState();

        std::ostringstream stream;
        stream << "[OnlineCompatibility] Built playerlobbies menu id=" << kPlayerLobbiesMenuId
               << " handler=" << Hex(reinterpret_cast<uintptr_t>(playerLobbiesHandler))
               << " object=" << Hex(static_cast<uintptr_t>(lobbyObject))
               << " display=" << Hex(lobbyDisplay)
               << " asset_id=" << playerLobbiesAssetId
               << " graphics=" << *reinterpret_cast<uint16_t*>(playerLobbiesHandler + kGraphicCountOffset)
               << ".";
        LogLine(stream.str());
        return lobbyObject != 0;
    }

    bool EnsurePlayerLobbiesHandler(uint32_t* self) {
        if (!self) {
            return false;
        }
        uintptr_t handlerTable = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(self) + kHandlerTableOffset);
        if (!handlerTable) {
            return false;
        }
        if (g_playerLobbiesHandlerBuilt) {
            return true;
        }

        auto* handlers = reinterpret_cast<uint8_t*>(handlerTable);
        auto* templateHandler = handlers + kPlayWithFriendsBuilderMenuId * kMenuHandlerSize;
        auto* playerLobbiesHandler = handlers + kPlayerLobbiesMenuId * kMenuHandlerSize;
        g_playerLobbiesHandlerBuilt = BuildPlayerLobbiesHandlerFromScratch(self, playerLobbiesHandler, templateHandler);
        return g_playerLobbiesHandlerBuilt;
    }
    bool PatchMenuBuilderHandlerCount() {
        constexpr uint32_t oldAllocationSize = static_cast<uint32_t>(sizeof(uint32_t) + kOriginalMenuHandlerCount * kMenuHandlerSize);
        constexpr uint32_t newAllocationSize = static_cast<uint32_t>(sizeof(uint32_t) + kExpandedMenuHandlerCount * kMenuHandlerSize);
        auto* code = reinterpret_cast<uint8_t*>(g_moduleBase + kMenuBuilderRva);

        intptr_t allocationOffset = -1;
        intptr_t storedCountOffset = -1;
        intptr_t constructorCountOffset = -1;

        for (size_t i = 0; i < 0x900; ++i) {
            if (allocationOffset < 0 && *reinterpret_cast<uint32_t*>(code + i) == oldAllocationSize) {
                allocationOffset = static_cast<intptr_t>(i);
            }

            if (storedCountOffset < 0 && i + 6 <= 0x900 && code[i] == 0xC7 &&
                (code[i + 1] == 0x06 || code[i + 1] == 0x07) &&
                code[i + 2] == kOriginalMenuHandlerCount && code[i + 3] == 0 && code[i + 4] == 0 && code[i + 5] == 0) {
                storedCountOffset = static_cast<intptr_t>(i + 2);
            }

            if (constructorCountOffset < 0 && i + 2 <= 0x900 && code[i] == 0x6A && code[i + 1] == kOriginalMenuHandlerCount) {
                const size_t start = i > 48 ? i - 48 : 0;
                const size_t end = (i + 96 < 0x900) ? i + 96 : 0x900;
                bool hasItemSize = false;
                bool hasCtor = false;
                bool hasDtor = false;
                for (size_t j = start; j + 4 <= end; ++j) {
                    const uint32_t value = *reinterpret_cast<uint32_t*>(code + j);
                    if (value == kMenuHandlerSize) {
                        hasItemSize = true;
                    }
                    if (value == g_moduleBase + kMenuConstructorRva) {
                        hasCtor = true;
                    }
                    if (value == g_moduleBase + kMenuDestructorRva) {
                        hasDtor = true;
                    }
                }
                if (hasItemSize && hasCtor && hasDtor) {
                    constructorCountOffset = static_cast<intptr_t>(i + 1);
                }
            }
        }

        std::ostringstream found;
        found << "[OnlineCompatibility] MenuBuilder patch scan alloc=" << Hex(allocationOffset >= 0 ? kMenuBuilderRva + allocationOffset : 0)
              << " stored=" << Hex(storedCountOffset >= 0 ? kMenuBuilderRva + storedCountOffset : 0)
              << " ctor=" << Hex(constructorCountOffset >= 0 ? kMenuBuilderRva + constructorCountOffset : 0) << ".";
        LogLine(found.str());

        if (allocationOffset < 0 || storedCountOffset < 0 || constructorCountOffset < 0) {
            LogLine("[OnlineCompatibility] MenuBuilder count patch skipped: exact offsets not found; leaving game code untouched.");
            return false;
        }

        std::vector<uint8_t> allocationBytes{
            static_cast<uint8_t>(newAllocationSize & 0xFF),
            static_cast<uint8_t>((newAllocationSize >> 8) & 0xFF),
            static_cast<uint8_t>((newAllocationSize >> 16) & 0xFF),
            static_cast<uint8_t>((newAllocationSize >> 24) & 0xFF)
        };
        const bool patchedAllocation = Util::MemoryPatcher::PatchBytes(kMenuBuilderRva + allocationOffset, allocationBytes);
        const bool patchedStoredCount = Util::MemoryPatcher::PatchBytes(kMenuBuilderRva + storedCountOffset, { static_cast<uint8_t>(kExpandedMenuHandlerCount) });
        const bool patchedConstructorCount = Util::MemoryPatcher::PatchBytes(kMenuBuilderRva + constructorCountOffset, { static_cast<uint8_t>(kExpandedMenuHandlerCount) });

        std::ostringstream stream;
        stream << "[OnlineCompatibility] MenuBuilder count patch allocation=" << (patchedAllocation ? "true" : "false")
               << " stored_count=" << (patchedStoredCount ? "true" : "false")
               << " ctor_count=" << (patchedConstructorCount ? "true" : "false")
               << " old_size=" << oldAllocationSize
               << " new_size=" << newAllocationSize << ".";
        LogLine(stream.str());
        return patchedAllocation && patchedStoredCount && patchedConstructorCount;
    }
    int __fastcall DetouredMenuBuilder(uint32_t* self, void*, int param2, int param3) {
        SetOnlineCompatibilityMode(OnlineCompatibilityMode::Unknown);
        g_expandedOwner = nullptr;
        g_expandedHandlers = nullptr;
        g_originalHandlers = 0;
        g_onlineCompatibilityHandlerBuilt = false;
        g_playerLobbiesHandlerBuilt = false;
        g_returnPlayWithFriendsToCompatibility = false;
        g_onlineCompatibilityGraphicObject = 0;
        ResetOnlineCompatibilityKeyboardState();

        const int result = g_originalMenuBuilder(self, param2, param3);
        uintptr_t handlerTable = self ? *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(self) + kHandlerTableOffset) : 0;
        if (handlerTable) {
            EnsureOnlineCompatibilityHandler(self, static_cast<int>(handlerTable + kOnlineCompatibilityMenuId * kMenuHandlerSize));
        }

        std::ostringstream stream;
        stream << "[OnlineCompatibility] MenuBuilder leave self=" << Hex(reinterpret_cast<uintptr_t>(self))
               << " table=" << Hex(handlerTable)
               << " registered_id=" << kOnlineCompatibilityMenuId << "/" << kPlayerLobbiesMenuId
               << " ready=" << (g_onlineCompatibilityHandlerBuilt ? "true" : "false") << ".";
        LogLine(stream.str());
        return result;
    }
    int __fastcall DetouredSetMenuState(uint32_t* self, void*, int16_t menuId, int menuState) {
        auto* ownerBytes = reinterpret_cast<uint8_t*>(self);
        uintptr_t handlerTable = self ? *reinterpret_cast<uintptr_t*>(ownerBytes + kHandlerTableOffset) : 0;
        uintptr_t currentHandler = self ? *reinterpret_cast<uintptr_t*>(ownerBytes + kCurrentHandlerOffset) : 0;
        const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
        const uintptr_t callsite = returnAddress >= 5 ? returnAddress - 5 : returnAddress;

        if (menuId == 23 || menuId == 33 || menuId == 34 || menuId == 36 || menuId == 37 || menuId == 42 || menuId == kOnlineLobbyMenuId || menuId == kOnlineCompatibilityMenuId || menuId == kPlayerLobbiesMenuId) {
            const uint16_t activeMenuId = self ? *reinterpret_cast<uint16_t*>(ownerBytes + 60) : 0;
            const uint16_t pendingMenuId = self ? *reinterpret_cast<uint16_t*>(ownerBytes + 608) : 0;
            const uint16_t transitionFromId = self ? *reinterpret_cast<uint16_t*>(ownerBytes + 90) : 0;
            const uint16_t returnTargetId = self ? *reinterpret_cast<uint16_t*>(ownerBytes + 550) : 0;
            std::ostringstream seen;
            seen << "[OnlineCompatibility] SetMenuState seen id=" << menuId
                 << " state=" << Hex(static_cast<uintptr_t>(menuState))
                 << " table=" << Hex(handlerTable)
                 << " expanded=" << Hex(reinterpret_cast<uintptr_t>(g_expandedHandlers))
                 << " current=" << Hex(currentHandler)
                 << " active_id=" << activeMenuId
                 << " pending_id=" << pendingMenuId
                 << " transition_from=" << transitionFromId
                 << " return_target=" << returnTargetId
                 << " caller_ret=" << Hex(returnAddress)
                 << " caller_rva=" << Hex(returnAddress - g_moduleBase)
                 << " callsite=" << Hex(callsite)
                 << " callsite_rva=" << Hex(callsite - g_moduleBase)
                 << " ready=" << (g_onlineCompatibilityHandlerBuilt ? "true" : "false")
                 << " mode=" << OnlineCompatibilityModeName(GetOnlineCompatibilityMode()) << ".";
            LogLine(seen.str());
        }

        if (menuId == 33 && GetOnlineCompatibilityMode() == OnlineCompatibilityMode::Unknown) {
            const uint16_t activeMenuId = self ? *reinterpret_cast<uint16_t*>(ownerBytes + 60) : 0;
            const uint16_t pendingMenuId = self ? *reinterpret_cast<uint16_t*>(ownerBytes + 608) : 0;
            if (g_onlineModeMenuArmed && activeMenuId == kModeMenuId && pendingMenuId == kPlayWithFriendsMenuId && g_onlineCompatibilityHandlerBuilt) {
                *reinterpret_cast<uint16_t*>(ownerBytes + 608) = kOnlineCompatibilityMenuId;
                g_onlineModeMenuArmed = false;
                std::ostringstream stream;
                stream << "[OnlineCompatibility] Patch transition pending menu " << kPlayWithFriendsMenuId
                       << " -> " << kOnlineCompatibilityMenuId
                       << " at transition id=33 active=" << activeMenuId << ".";
                LogLine(stream.str());
            }
        }
        if (menuId == kModeMenuId) {
            const uintptr_t callerRva = returnAddress - g_moduleBase;
            g_onlineModeMenuArmed = callerRva == kOpenOnlineModeMenuCallerRva;
            if (g_onlineModeMenuArmed) {
                EnsureOnlineCompatibilityHandler(self, menuState);
                handlerTable = self ? *reinterpret_cast<uintptr_t*>(ownerBytes + kHandlerTableOffset) : 0;
                if (g_onlineCompatibilityHandlerBuilt && handlerTable) {
                    menuState = static_cast<int>(handlerTable + kModeMenuId * kMenuHandlerSize);
                }
                currentHandler = self ? *reinterpret_cast<uintptr_t*>(ownerBytes + kCurrentHandlerOffset) : 0;
            }
            std::ostringstream stream;
            stream << "[OnlineCompatibility] Mode menu source armed=" << (g_onlineModeMenuArmed ? "true" : "false")
                   << " caller_rva=" << Hex(callerRva)
                   << " ready=" << (g_onlineCompatibilityHandlerBuilt ? "true" : "false") << ".";
            LogLine(stream.str());
        } else if (menuId == 23) {
            g_onlineModeMenuArmed = false;
        }
        if (menuId == 23 && g_suppressNextModeMenuBack) {
            const uintptr_t modeMenuState = handlerTable ? handlerTable + kModeMenuId * kMenuHandlerSize : 0;
            if (currentHandler == modeMenuState) {
                g_suppressNextModeMenuBack = false;
                LogLine("[OnlineCompatibility] Suppress inherited modemenu back after compatibility escape.");
                return reinterpret_cast<int>(self);
            }
        }

        if (menuId == kModeMenuId && (GetOnlineCompatibilityMode() != OnlineCompatibilityMode::Unknown || g_returnPlayWithFriendsToCompatibility)) {
            const uintptr_t playWithFriendsState = handlerTable ? handlerTable + kPlayWithFriendsBuilderMenuId * kMenuHandlerSize : 0;
            if (currentHandler == playWithFriendsState) {
                EnsureOnlineCompatibilityHandler(self, static_cast<int>(playWithFriendsState));
                handlerTable = self ? *reinterpret_cast<uintptr_t*>(ownerBytes + kHandlerTableOffset) : 0;
                int compatibilityState = static_cast<int>(handlerTable + kOnlineCompatibilityMenuId * kMenuHandlerSize);
                std::ostringstream stream;
                stream << "[OnlineCompatibility] Redirect playwithfriends back " << kModeMenuId
                       << " -> " << kOnlineCompatibilityMenuId
                       << " state=" << Hex(static_cast<uintptr_t>(compatibilityState)) << ".";
                LogLine(stream.str());
                SetOnlineCompatibilityMode(OnlineCompatibilityMode::Unknown);
                g_returnPlayWithFriendsToCompatibility = false;
                ResetOnlineCompatibilityKeyboardState();
                return g_originalSetMenuState(self, static_cast<int16_t>(kOnlineCompatibilityMenuId), compatibilityState);
            }
        }

        if (menuId == kModeMenuId) {
            SetOnlineCompatibilityMode(OnlineCompatibilityMode::Unknown);
            ResetOnlineCompatibilityKeyboardState();
        }
if (menuId == kOnlineLobbyMenuId && GetOnlineCompatibilityMode() == OnlineCompatibilityMode::Unknown) {
            const uintptr_t compatibilityState = handlerTable ? handlerTable + kOnlineCompatibilityMenuId * kMenuHandlerSize : 0;
            if (currentHandler == compatibilityState) {
                uint16_t selectedIndex = *reinterpret_cast<uint16_t*>(compatibilityState + kMenuSelectionOffset);
                OnlineCompatibilityMode mode = selectedIndex == 0 ? OnlineCompatibilityMode::Vanilla : OnlineCompatibilityMode::Modded;
                std::ostringstream stream;
                stream << "[OnlineCompatibility] Intercept lobby from compatibility selected=" << selectedIndex
                       << " mode=" << OnlineCompatibilityModeName(mode) << ".";
                LogLine(stream.str());
                return OpenPlayWithFriendsWithMode(self, mode);
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
        std::ostringstream leave;
        leave << "[OnlineCompatibility] BuildPlayWithFriendsMenu leave result=" << Hex(static_cast<uintptr_t>(result))
              << " handler_ready=" << (g_onlineCompatibilityHandlerBuilt ? "true" : "false")
              << " mode=" << OnlineCompatibilityModeName(GetOnlineCompatibilityMode()) << ".";
        LogLine(leave.str());
        return result;
    }
}

int GetPlayerLobbySelectedFrame() {
    return g_playerLobbiesSelectedIndex < 0 ? 1 : g_playerLobbiesSelectedIndex + 2;
}

bool SetupMenuOnlineCompatibilityHook() {
    g_originalMenuBuilder = reinterpret_cast<MenuBuilderFn>(g_moduleBase + kMenuBuilderRva);
    g_originalBuildOnlineMenu = reinterpret_cast<BuildOnlineMenuFn>(g_moduleBase + kBuildOnlineMenuRva);
    g_originalSetMenuState = reinterpret_cast<SetMenuStateFn>(g_moduleBase + kSetMenuStateRva);
    g_bootstrapAllocator = reinterpret_cast<BootstrapAllocatorFn>(g_moduleBase + kBootstrapAllocatorRva);
    g_initAllocator = reinterpret_cast<InitAllocatorFn>(g_moduleBase + kInitAllocatorRva);
    g_getAllocator = reinterpret_cast<GetAllocatorFn>(g_moduleBase + kGetAllocatorRva);
    g_allocate = reinterpret_cast<AllocateFn>(g_moduleBase + kAllocateRva);
    g_prepareStringObject = reinterpret_cast<PrepareStringObjectFn>(g_moduleBase + kPrepareStringObjectRva);
    g_graphicMenuItem = reinterpret_cast<GraphicMenuItemFn>(g_moduleBase + kGraphicMenuItemRva);
    g_initGraphicMenuItem = reinterpret_cast<InitGraphicMenuItemFn>(g_moduleBase + kInitGraphicMenuItemRva);
    g_buttonMenuItemSetPreset = reinterpret_cast<ButtonMenuItemSetPresetFn>(g_moduleBase + kButtonMenuItemSetPresetRva);
    g_finalizeAndAddElement = reinterpret_cast<FinalizeAndAddElementFn>(g_moduleBase + kFinalizeAndAddElementRva);
    g_gotoAndStop = reinterpret_cast<GotoAndStopFn>(g_moduleBase + kGotoAndStopRva);
    g_menuConfirmSound = g_moduleBase + kMenuConfirmSoundRva;
    g_startMenuTransition = reinterpret_cast<StartMenuTransitionFn>(g_moduleBase + kStartMenuTransitionRva);
    g_startPlayWithFriendsTransition = reinterpret_cast<StartPlayWithFriendsTransitionFn>(g_moduleBase + kStartPlayWithFriendsTransitionRva);
    g_constructMenuArray = reinterpret_cast<ConstructMenuArrayFn>(g_moduleBase + kConstructMenuArrayRva);

    if (!PatchMenuBuilderHandlerCount()) {
        LogLine("[OnlineCompatibility] MenuBuilder handler-count patch unavailable; online compatibility menu hook disabled for this run.");
        return true;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG attachResult = DetourAttach(&(PVOID&)g_originalMenuBuilder, DetouredMenuBuilder);
    if (attachResult == NO_ERROR) {
        attachResult = DetourAttach(&(PVOID&)g_originalBuildOnlineMenu, DetouredBuildOnlineMenu);
    }
    if (attachResult == NO_ERROR) {
        attachResult = DetourAttach(&(PVOID&)g_originalSetMenuState, DetouredSetMenuState);
    }
    if (attachResult != NO_ERROR) {
        OutputDebugStringA("[OnlineCompatibility] Menu detour attach failed.\n");
        DetourTransactionAbort();
        return false;
    }

    LONG commitResult = DetourTransactionCommit();
    if (commitResult != NO_ERROR) {
        OutputDebugStringA("[OnlineCompatibility] Menu detour commit failed.\n");
        return false;
    }

    std::ostringstream stream;
    stream << "[OnlineCompatibility] Hook installed menubuilder=" << Hex(kMenuBuilderRva)
           << " build_playwithfriends=" << Hex(kBuildOnlineMenuRva)
           << " set_menu=" << Hex(kSetMenuStateRva)
           << " graphic=" << Hex(kGraphicMenuItemRva)
           << " finalize=" << Hex(kFinalizeAndAddElementRva) << ".";
    LogLine(stream.str());
    return true;
}

} // namespace HookCrashers::Save










































