#pragma once

#include <cstddef>
#include <cstdint>

namespace HookCrashers::Game {

using MenuCallback = void*;

struct MenuHotspot {
    float left;          // +0x00
    float top;           // +0x04
    float right;         // +0x08
    float bottom;        // +0x0C
    std::uint8_t enabled; // +0x10
    std::uint8_t pad_11[3];
    std::uint32_t field_14;
    std::uint32_t field_18;
    void* owner;         // +0x1C
    MenuCallback onSelect; // +0x20
    MenuCallback field_24;
    MenuCallback onClick;  // +0x28
    void* field_2C;
    std::uint8_t flags;  // +0x30
    std::uint8_t pad_31[3];
};

struct MenuItemBase {
    void* vtable;          // +0x00
    std::uint8_t pad_04[0x0C];
    std::uint32_t color;   // +0x10
    std::uint32_t fallbackColor; // +0x14
};

// Verified against the TextMenuItem constructor (RVA 0x153EE0) and the confirm
// action (RVA 0x152E50), which calls `callback(owner)` as __cdecl.
struct TextMenuItem {
    MenuItemBase base;
    std::uint8_t pad_18[0x14];
    std::uint8_t autoLayout;   // +0x2C, laid out by FinalizeAndAddElement when set
    std::uint8_t disabled;     // +0x2D
    std::uint8_t selectable;   // +0x2E
    std::uint8_t visible;      // +0x2F
    std::uint8_t pad_30[4];
    void* owner;               // +0x34, argument passed to callback
    MenuCallback callback;     // +0x38
    wchar_t* text;             // +0x3C, allocated with the game allocator
    float fontScale;           // +0x40
    float fitScale;            // +0x44
    std::uint16_t alignment;   // +0x48
    std::uint16_t textLength;  // +0x4A
    std::uint16_t visualWidth; // +0x4C
    std::uint16_t maxWidth;    // +0x4E
};

struct GraphicMenuItem {
    MenuItemBase base;
    std::uint8_t pad_18[0x3C];
    void* displayObject;     // +0x54
    std::uint32_t field_58;
};

struct ButtonMenuItem {
    MenuItemBase base;
    std::uint8_t pad_18[0x44];
    std::uint32_t presetId;  // +0x5C
};

struct MenuState {
    void* vtable;                    // +0x000
    std::uint16_t menuItemCount;      // +0x004
    std::uint16_t graphicCount;       // +0x006
    std::uint16_t buttonCount;        // +0x008
    std::uint16_t hotspotCount;       // +0x00A
    std::uint16_t selectedIndex;      // +0x00C
    std::uint16_t field_0E;
    std::uint16_t field_10;
    std::uint16_t field_12;
    std::uint8_t field_14;
    std::uint8_t inputEnabled;        // +0x015
    std::uint8_t pad_16[0x06];
    MenuHotspot* hotspots[10];        // +0x01C
    void* menuItems[64];              // +0x044
    GraphicMenuItem* graphics[16];    // +0x144
    ButtonMenuItem* buttons[8];       // +0x184
    void* field_1A4;
    void* field_1A8;
    void* owner;                      // +0x1AC
    void* field_1B0;
    MenuCallback backCallback;        // +0x1B4
    void* field_1B8;
    void* field_1BC;
    void* field_1C0;
    void* field_1C4;
    MenuCallback updateCallback;      // +0x1C8
    void* field_1CC;
    MenuCallback initCallback;        // +0x1D0
    std::uint8_t pad_1D4[0x06];
    std::uint16_t configuredTargetMenuId; // +0x1DA
    std::uint8_t pad_1DC[0x30];
};

struct MainMenu {
    void* vtable;             // +0x00
    void* flashController;    // +0x04
    void* scene;              // +0x08
    std::uint8_t pad_0C[0x2C];
    MenuState* currentMenu;   // +0x38
    std::uint16_t activeMenuId; // +0x3C
    std::uint8_t pad_3E[2];
    MenuState* menuTable;     // +0x40
};

struct MenuTableAllocation {
    std::uint32_t count;
    MenuState entries[1];
};

#if INTPTR_MAX == INT32_MAX
static_assert(sizeof(MenuHotspot) == 0x34);
static_assert(sizeof(TextMenuItem) == 0x50);
static_assert(sizeof(GraphicMenuItem) == 0x5C);
static_assert(sizeof(ButtonMenuItem) == 0x60);
static_assert(sizeof(MenuState) == 0x20C);
static_assert(offsetof(MenuHotspot, owner) == 0x1C);
static_assert(offsetof(MenuHotspot, onSelect) == 0x20);
static_assert(offsetof(MenuHotspot, onClick) == 0x28);
static_assert(offsetof(MenuHotspot, flags) == 0x30);
static_assert(offsetof(MenuItemBase, color) == 0x10);
static_assert(offsetof(TextMenuItem, autoLayout) == 0x2C);
static_assert(offsetof(TextMenuItem, disabled) == 0x2D);
static_assert(offsetof(TextMenuItem, selectable) == 0x2E);
static_assert(offsetof(TextMenuItem, visible) == 0x2F);
static_assert(offsetof(TextMenuItem, owner) == 0x34);
static_assert(offsetof(TextMenuItem, callback) == 0x38);
static_assert(offsetof(TextMenuItem, text) == 0x3C);
static_assert(offsetof(TextMenuItem, maxWidth) == 0x4E);
static_assert(offsetof(GraphicMenuItem, displayObject) == 0x54);
static_assert(offsetof(ButtonMenuItem, presetId) == 0x5C);
static_assert(offsetof(MenuState, menuItemCount) == 0x04);
static_assert(offsetof(MenuState, graphicCount) == 0x06);
static_assert(offsetof(MenuState, buttonCount) == 0x08);
static_assert(offsetof(MenuState, hotspotCount) == 0x0A);
static_assert(offsetof(MenuState, selectedIndex) == 0x0C);
static_assert(offsetof(MenuState, hotspots) == 0x1C);
static_assert(offsetof(MenuState, menuItems) == 0x44);
static_assert(offsetof(MenuState, graphics) == 0x144);
static_assert(offsetof(MenuState, buttons) == 0x184);
static_assert(offsetof(MenuState, owner) == 0x1AC);
static_assert(offsetof(MenuState, backCallback) == 0x1B4);
static_assert(offsetof(MenuState, updateCallback) == 0x1C8);
static_assert(offsetof(MenuState, initCallback) == 0x1D0);
static_assert(offsetof(MenuState, configuredTargetMenuId) == 0x1DA);
static_assert(offsetof(MainMenu, currentMenu) == 0x38);
static_assert(offsetof(MainMenu, activeMenuId) == 0x3C);
static_assert(offsetof(MainMenu, menuTable) == 0x40);
#endif

} // namespace HookCrashers::Game
