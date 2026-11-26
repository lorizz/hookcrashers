#pragma once

// Platform-independent part of the Menu API: menu definitions, validation and
// value state. The native side (MenuNative.cpp) turns a Menu into game objects.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace HookCrashers::UI::Menus {

enum class ItemKind : int {
    Button = 0,   // activates its callback
    Toggle = 1,   // value 0 (off) / 1 (on), changed with left/right or confirm
    Choice = 2,   // value is the selected option index
    Submenu = 3,  // opens another registered menu
    Label = 4,    // non-selectable text
    Graphic = 5,  // exported clip from a registered menu graphic
};

const char* ItemKindName(ItemKind kind);

enum class Result : int {
    Ok = 0,
    InvalidArgument,
    DuplicateId,
    UnknownMenu,
    UnknownItem,
    LimitReached,
    WrongKind,
};

const char* ResultName(Result result);

// Native layout limits (MenuState arrays and screen space, see docs/menu-api.md).
constexpr std::size_t kMaxMenus = 16;          // Game::MenuTable::kModMenuSlotCount
constexpr std::size_t kMaxTextItems = 14;      // buttons, toggles, choices, submenus, labels
constexpr std::size_t kMaxGraphics = 12;       // graphics array holds 16; background + headroom
constexpr std::size_t kMaxOptions = 32;
constexpr std::size_t kMaxIdLength = 64;
constexpr std::size_t kMaxTextBytes = 256;

// value: Button/Submenu 0, Toggle 0/1, Choice option index.
using ItemCallback = std::function<void(int value)>;
using CloseCallback = std::function<void()>;

struct Item {
    std::string id;
    ItemKind kind = ItemKind::Button;
    std::string label;                 // UTF-8
    std::vector<std::string> options;  // Choice options; Toggle {off, on} labels
    int value = 0;
    bool enabled = true;
    std::string target;                // Submenu: menu id to open
    std::string graphic;               // Graphic: registered graphic id
    float x = 0.0f;                    // Graphic: position in menu coordinates
    float y = 0.0f;
    std::uint32_t color = 0;           // 0xRRGGBBAA text color, 0 = game default (grey when disabled)
    ItemCallback onActivate;
};

struct Menu {
    std::string id;
    std::string title;                 // UTF-8, may be empty
    std::vector<Item> items;
    CloseCallback onClose;
    bool background = true;            // native "bookbg" backdrop
    bool backButton = true;            // Back button + Back key; false: the menu can only be left from its items
    std::uint32_t revision = 1;        // bumped on structural changes (rebuild needed)
};

bool IsValidId(const std::string& id);
bool IsTextItem(ItemKind kind);
bool IsSelectable(const Item& item);
std::size_t CountTextItems(const Menu& menu);
std::size_t CountGraphics(const Menu& menu);
int NormalizeValue(const Item& item, int value);
std::vector<std::string> DefaultToggleLabels();

class Registry {
public:
    static Registry& Instance();

    Result CreateMenu(const std::string& id, const std::string& title);
    Result AddItem(const std::string& menuId, Item item);
    Result SetTitle(const std::string& menuId, const std::string& title);
    Result SetBackground(const std::string& menuId, bool enabled);
    Result SetBackButton(const std::string& menuId, bool enabled);
    Result SetOnClose(const std::string& menuId, CloseCallback onClose);

    // Live updates: text/value/enabled do not require a rebuild.
    Result SetItemText(const std::string& menuId, const std::string& itemId, const std::string& text);
    Result SetItemValue(const std::string& menuId, const std::string& itemId, int value);
    Result SetItemEnabled(const std::string& menuId, const std::string& itemId, bool enabled);
    Result SetItemColor(const std::string& menuId, const std::string& itemId, std::uint32_t rgba);
    // Choice/Toggle labels. Same option count: live update; a different count
    // is structural and bumps the revision (the menu is rebuilt on next open).
    Result SetItemOptions(const std::string& menuId, const std::string& itemId, std::vector<std::string> options);
    Result GetItemValue(const std::string& menuId, const std::string& itemId, int& value) const;

    // Thread-safe copies for the native builder and for dispatch.
    bool Snapshot(const std::string& menuId, Menu& out) const;
    bool ItemAt(const std::string& menuId, std::size_t index, Item& out) const;
    // Records a value changed by the game (left/right on a toggle/choice).
    Result CommitValue(const std::string& menuId, std::size_t index, int value, Item& out);
    bool Exists(const std::string& menuId) const;
    std::size_t MenuCount() const;

    void Clear(); // tests and shutdown

private:
    Menu* FindMenu(const std::string& id);
    const Menu* FindMenu(const std::string& id) const;

    mutable std::mutex m_lock;
    std::vector<Menu> m_menus;
};

} // namespace HookCrashers::UI::Menus
