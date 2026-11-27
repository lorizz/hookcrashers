#include "MenuModel.h"

#include <algorithm>
#include <utility>

namespace HookCrashers::UI::Menus {
namespace {

Item* FindItem(Menu& menu, const std::string& itemId) {
    for (Item& item : menu.items) {
        if (item.id == itemId) return &item;
    }
    return nullptr;
}

const Item* FindItem(const Menu& menu, const std::string& itemId) {
    for (const Item& item : menu.items) {
        if (item.id == itemId) return &item;
    }
    return nullptr;
}

bool ValidText(const std::string& text) {
    return text.size() <= kMaxTextBytes;
}

} // namespace

const char* ItemKindName(ItemKind kind) {
    switch (kind) {
    case ItemKind::Button: return "button";
    case ItemKind::Toggle: return "toggle";
    case ItemKind::Choice: return "choice";
    case ItemKind::Submenu: return "submenu";
    case ItemKind::Label: return "label";
    case ItemKind::Graphic: return "graphic";
    }
    return "unknown";
}

const char* ResultName(Result result) {
    switch (result) {
    case Result::Ok: return "ok";
    case Result::InvalidArgument: return "invalid argument";
    case Result::DuplicateId: return "duplicate id";
    case Result::UnknownMenu: return "unknown menu";
    case Result::UnknownItem: return "unknown item";
    case Result::LimitReached: return "limit reached";
    case Result::WrongKind: return "wrong item kind";
    }
    return "unknown";
}

bool IsValidId(const std::string& id) {
    if (id.empty() || id.size() > kMaxIdLength) return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '.' || c == '-' || c == ':';
    });
}

bool IsTextItem(ItemKind kind) {
    return kind != ItemKind::Graphic;
}

bool IsSelectable(const Item& item) {
    return item.kind != ItemKind::Label && item.kind != ItemKind::Graphic;
}

std::size_t CountTextItems(const Menu& menu) {
    return static_cast<std::size_t>(std::count_if(menu.items.begin(), menu.items.end(),
        [](const Item& item) { return IsTextItem(item.kind); }));
}

std::size_t CountGraphics(const Menu& menu) {
    return static_cast<std::size_t>(std::count_if(menu.items.begin(), menu.items.end(),
        [](const Item& item) { return item.kind == ItemKind::Graphic; }));
}

int NormalizeValue(const Item& item, int value) {
    switch (item.kind) {
    case ItemKind::Toggle:
        return value != 0 ? 1 : 0;
    case ItemKind::Choice:
        if (item.options.empty()) return 0;
        return (std::clamp)(value, 0, static_cast<int>(item.options.size()) - 1);
    default:
        return 0;
    }
}

std::vector<std::string> DefaultToggleLabels() {
    return {"Off", "On"};
}

Registry& Registry::Instance() {
    static Registry registry;
    return registry;
}

Menu* Registry::FindMenu(const std::string& id) {
    for (Menu& menu : m_menus) {
        if (menu.id == id) return &menu;
    }
    return nullptr;
}

const Menu* Registry::FindMenu(const std::string& id) const {
    for (const Menu& menu : m_menus) {
        if (menu.id == id) return &menu;
    }
    return nullptr;
}

Result Registry::CreateMenu(const std::string& id, const std::string& title) {
    if (!IsValidId(id) || !ValidText(title)) return Result::InvalidArgument;
    std::lock_guard<std::mutex> lock(m_lock);
    if (FindMenu(id)) return Result::DuplicateId;
    if (m_menus.size() >= kMaxMenus) return Result::LimitReached;
    Menu menu;
    menu.id = id;
    menu.title = title;
    m_menus.push_back(std::move(menu));
    return Result::Ok;
}

Result Registry::AddItem(const std::string& menuId, Item item) {
    if (!IsValidId(item.id) || !ValidText(item.label)) return Result::InvalidArgument;
    switch (item.kind) {
    case ItemKind::Toggle:
        if (item.options.empty()) item.options = DefaultToggleLabels();
        if (item.options.size() != 2) return Result::InvalidArgument;
        break;
    case ItemKind::Choice:
        if (item.options.empty() || item.options.size() > kMaxOptions) return Result::InvalidArgument;
        break;
    case ItemKind::Submenu:
        if (!IsValidId(item.target) || item.target == menuId) return Result::InvalidArgument;
        item.options.clear();
        break;
    case ItemKind::Graphic:
        if (!IsValidId(item.graphic)) return Result::InvalidArgument;
        item.options.clear();
        break;
    case ItemKind::Button:
    case ItemKind::Label:
        item.options.clear();
        break;
    default:
        return Result::InvalidArgument;
    }
    for (const std::string& option : item.options) {
        if (option.empty() || !ValidText(option)) return Result::InvalidArgument;
    }
    item.value = NormalizeValue(item, item.value);

    std::lock_guard<std::mutex> lock(m_lock);
    Menu* menu = FindMenu(menuId);
    if (!menu) return Result::UnknownMenu;
    if (FindItem(*menu, item.id)) return Result::DuplicateId;
    if (IsTextItem(item.kind) && CountTextItems(*menu) >= kMaxTextItems) return Result::LimitReached;
    if (item.kind == ItemKind::Graphic && CountGraphics(*menu) >= kMaxGraphics) return Result::LimitReached;
    menu->items.push_back(std::move(item));
    ++menu->revision;
    return Result::Ok;
}

Result Registry::SetTitle(const std::string& menuId, const std::string& title) {
    if (!ValidText(title)) return Result::InvalidArgument;
    std::lock_guard<std::mutex> lock(m_lock);
    Menu* menu = FindMenu(menuId);
    if (!menu) return Result::UnknownMenu;
    menu->title = title;
    ++menu->revision;
    return Result::Ok;
}

Result Registry::SetBackground(const std::string& menuId, bool enabled) {
    std::lock_guard<std::mutex> lock(m_lock);
    Menu* menu = FindMenu(menuId);
    if (!menu) return Result::UnknownMenu;
    if (menu->background != enabled) {
        menu->background = enabled;
        ++menu->revision;
    }
    return Result::Ok;
}

Result Registry::SetBackButton(const std::string& menuId, bool enabled) {
    std::lock_guard<std::mutex> lock(m_lock);
    Menu* menu = FindMenu(menuId);
    if (!menu) return Result::UnknownMenu;
    if (menu->backButton != enabled) {
        menu->backButton = enabled;
        ++menu->revision;
    }
    return Result::Ok;
}

Result Registry::SetOnClose(const std::string& menuId, CloseCallback onClose) {
    std::lock_guard<std::mutex> lock(m_lock);
    Menu* menu = FindMenu(menuId);
    if (!menu) return Result::UnknownMenu;
    menu->onClose = std::move(onClose);
    return Result::Ok;
}

Result Registry::SetItemText(const std::string& menuId, const std::string& itemId, const std::string& text) {
    if (!ValidText(text)) return Result::InvalidArgument;
    std::lock_guard<std::mutex> lock(m_lock);
    Menu* menu = FindMenu(menuId);
    if (!menu) return Result::UnknownMenu;
    Item* item = FindItem(*menu, itemId);
    if (!item) return Result::UnknownItem;
    if (item->kind == ItemKind::Graphic) return Result::WrongKind;
    item->label = text;
    return Result::Ok;
}

Result Registry::SetItemValue(const std::string& menuId, const std::string& itemId, int value) {
    std::lock_guard<std::mutex> lock(m_lock);
    Menu* menu = FindMenu(menuId);
    if (!menu) return Result::UnknownMenu;
    Item* item = FindItem(*menu, itemId);
    if (!item) return Result::UnknownItem;
    if (item->kind != ItemKind::Toggle && item->kind != ItemKind::Choice) return Result::WrongKind;
    item->value = NormalizeValue(*item, value);
    return Result::Ok;
}

Result Registry::SetItemEnabled(const std::string& menuId, const std::string& itemId, bool enabled) {
    std::lock_guard<std::mutex> lock(m_lock);
    Menu* menu = FindMenu(menuId);
    if (!menu) return Result::UnknownMenu;
    Item* item = FindItem(*menu, itemId);
    if (!item) return Result::UnknownItem;
    if (!IsSelectable(*item)) return Result::WrongKind;
    item->enabled = enabled;
    return Result::Ok;
}

Result Registry::SetItemColor(const std::string& menuId, const std::string& itemId, std::uint32_t rgba) {
    std::lock_guard<std::mutex> lock(m_lock);
    Menu* menu = FindMenu(menuId);
    if (!menu) return Result::UnknownMenu;
    Item* item = FindItem(*menu, itemId);
    if (!item) return Result::UnknownItem;
    if (item->kind == ItemKind::Graphic) return Result::WrongKind;
    item->color = rgba;
    return Result::Ok;
}

Result Registry::SetItemOptions(const std::string& menuId, const std::string& itemId, std::vector<std::string> options) {
    if (options.empty() || options.size() > kMaxOptions) return Result::InvalidArgument;
    for (const std::string& option : options) {
        if (option.empty() || !ValidText(option)) return Result::InvalidArgument;
    }
    std::lock_guard<std::mutex> lock(m_lock);
    Menu* menu = FindMenu(menuId);
    if (!menu) return Result::UnknownMenu;
    Item* item = FindItem(*menu, itemId);
    if (!item) return Result::UnknownItem;
    if (item->kind != ItemKind::Toggle && item->kind != ItemKind::Choice) return Result::WrongKind;
    if (item->kind == ItemKind::Toggle && options.size() != 2) return Result::InvalidArgument;
    const bool structural = options.size() != item->options.size();
    item->options = std::move(options);
    item->value = NormalizeValue(*item, item->value);
    if (structural) ++menu->revision;
    return Result::Ok;
}

Result Registry::GetItemValue(const std::string& menuId, const std::string& itemId, int& value) const {
    std::lock_guard<std::mutex> lock(m_lock);
    const Menu* menu = FindMenu(menuId);
    if (!menu) return Result::UnknownMenu;
    const Item* item = FindItem(*menu, itemId);
    if (!item) return Result::UnknownItem;
    value = item->value;
    return Result::Ok;
}

bool Registry::Snapshot(const std::string& menuId, Menu& out) const {
    std::lock_guard<std::mutex> lock(m_lock);
    const Menu* menu = FindMenu(menuId);
    if (!menu) return false;
    out = *menu;
    return true;
}

bool Registry::ItemAt(const std::string& menuId, std::size_t index, Item& out) const {
    std::lock_guard<std::mutex> lock(m_lock);
    const Menu* menu = FindMenu(menuId);
    if (!menu || index >= menu->items.size()) return false;
    out = menu->items[index];
    return true;
}

Result Registry::CommitValue(const std::string& menuId, std::size_t index, int value, Item& out) {
    std::lock_guard<std::mutex> lock(m_lock);
    Menu* menu = FindMenu(menuId);
    if (!menu) return Result::UnknownMenu;
    if (index >= menu->items.size()) return Result::UnknownItem;
    Item& item = menu->items[index];
    if (item.kind != ItemKind::Toggle && item.kind != ItemKind::Choice) return Result::WrongKind;
    item.value = NormalizeValue(item, value);
    out = item;
    return Result::Ok;
}

bool Registry::Exists(const std::string& menuId) const {
    std::lock_guard<std::mutex> lock(m_lock);
    return FindMenu(menuId) != nullptr;
}

std::size_t Registry::MenuCount() const {
    std::lock_guard<std::mutex> lock(m_lock);
    return m_menus.size();
}

void Registry::Clear() {
    std::lock_guard<std::mutex> lock(m_lock);
    m_menus.clear();
}

} // namespace HookCrashers::UI::Menus
