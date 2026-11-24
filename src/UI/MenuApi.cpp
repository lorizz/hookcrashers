#include "MenuApi.h"

#include "../Core/Logger.h"
#include "../SWF/MenuGraphicAssets.h"

#include <windows.h>

#include <fstream>
#include <iterator>
#include <utility>

#if defined(_M_IX86)
#pragma comment(linker, "/EXPORT:HookCrashers_MenuCreate=_HookCrashers_MenuCreate@8")
#pragma comment(linker, "/EXPORT:HookCrashers_MenuAddButton=_HookCrashers_MenuAddButton@20")
#pragma comment(linker, "/EXPORT:HookCrashers_MenuAddToggle=_HookCrashers_MenuAddToggle@24")
#pragma comment(linker, "/EXPORT:HookCrashers_MenuAddChoice=_HookCrashers_MenuAddChoice@32")
#pragma comment(linker, "/EXPORT:HookCrashers_MenuAddSubmenu=_HookCrashers_MenuAddSubmenu@16")
#pragma comment(linker, "/EXPORT:HookCrashers_MenuAddLabel=_HookCrashers_MenuAddLabel@12")
#pragma comment(linker, "/EXPORT:HookCrashers_MenuAddGraphic=_HookCrashers_MenuAddGraphic@20")
#pragma comment(linker, "/EXPORT:HookCrashers_MenuSetOnClose=_HookCrashers_MenuSetOnClose@12")
#pragma comment(linker, "/EXPORT:HookCrashers_MenuSetBackground=_HookCrashers_MenuSetBackground@8")
#pragma comment(linker, "/EXPORT:HookCrashers_MenuOpen=_HookCrashers_MenuOpen@4")
#pragma comment(linker, "/EXPORT:HookCrashers_MenuClose=_HookCrashers_MenuClose@0")
#pragma comment(linker, "/EXPORT:HookCrashers_MenuSetItemText=_HookCrashers_MenuSetItemText@12")
#pragma comment(linker, "/EXPORT:HookCrashers_MenuSetItemValue=_HookCrashers_MenuSetItemValue@12")
#pragma comment(linker, "/EXPORT:HookCrashers_MenuGetItemValue=_HookCrashers_MenuGetItemValue@8")
#pragma comment(linker, "/EXPORT:HookCrashers_MenuSetItemEnabled=_HookCrashers_MenuSetItemEnabled@12")
#pragma comment(linker, "/EXPORT:HookCrashers_RegisterMenuGraphic=_HookCrashers_RegisterMenuGraphic@12")
#endif

namespace HookCrashers::UI::Menus::Api {
namespace {

bool Report(Result result, const char* action, const std::string& menuId, const std::string& itemId = {}) {
    if (result == Result::Ok) return true;
    Core::Logger::Error("Menu API: %s failed for menu '%s'%s%s%s: %s", action, menuId.c_str(),
                        itemId.empty() ? "" : " item '", itemId.c_str(), itemId.empty() ? "" : "'",
                        ResultName(result));
    return false;
}

bool Add(const std::string& menuId, Item item, const char* action) {
    const std::string itemId = item.id;
    return Report(Registry::Instance().AddItem(menuId, std::move(item)), action, menuId, itemId);
}

bool Live(Result result, const char* action, const std::string& menuId, const std::string& itemId) {
    if (!Report(result, action, menuId, itemId)) return false;
    Native::MarkDirty(menuId);
    return true;
}

} // namespace

bool CreateMenu(const std::string& menuId, const std::string& title) {
    return Report(Registry::Instance().CreateMenu(menuId, title), "CreateMenu", menuId);
}

bool AddButton(const std::string& menuId, const std::string& itemId, const std::string& label, ItemCallback onClick) {
    Item item;
    item.id = itemId;
    item.kind = ItemKind::Button;
    item.label = label;
    item.onActivate = std::move(onClick);
    return Add(menuId, std::move(item), "AddButton");
}

bool AddToggle(const std::string& menuId, const std::string& itemId, const std::string& label, bool initial,
               ItemCallback onChange, std::vector<std::string> labels) {
    Item item;
    item.id = itemId;
    item.kind = ItemKind::Toggle;
    item.label = label;
    item.value = initial ? 1 : 0;
    item.options = std::move(labels);
    item.onActivate = std::move(onChange);
    return Add(menuId, std::move(item), "AddToggle");
}

bool AddChoice(const std::string& menuId, const std::string& itemId, const std::string& label,
               std::vector<std::string> options, int initialIndex, ItemCallback onChange) {
    Item item;
    item.id = itemId;
    item.kind = ItemKind::Choice;
    item.label = label;
    item.options = std::move(options);
    item.value = initialIndex;
    item.onActivate = std::move(onChange);
    return Add(menuId, std::move(item), "AddChoice");
}

bool AddSubmenu(const std::string& menuId, const std::string& itemId, const std::string& label, const std::string& targetMenuId) {
    Item item;
    item.id = itemId;
    item.kind = ItemKind::Submenu;
    item.label = label;
    item.target = targetMenuId;
    return Add(menuId, std::move(item), "AddSubmenu");
}

bool AddLabel(const std::string& menuId, const std::string& itemId, const std::string& text) {
    Item item;
    item.id = itemId;
    item.kind = ItemKind::Label;
    item.label = text;
    return Add(menuId, std::move(item), "AddLabel");
}

bool AddGraphic(const std::string& menuId, const std::string& itemId, const std::string& graphicId, float x, float y) {
    if (!SWF::MenuGraphicAssets::IsRegistered(graphicId)) {
        Core::Logger::Error("Menu API: AddGraphic for menu '%s': graphic '%s' is not registered",
                            menuId.c_str(), graphicId.c_str());
        return false;
    }
    Item item;
    item.id = itemId;
    item.kind = ItemKind::Graphic;
    item.graphic = graphicId;
    item.x = x;
    item.y = y;
    return Add(menuId, std::move(item), "AddGraphic");
}

bool SetOnClose(const std::string& menuId, CloseCallback onClose) {
    return Report(Registry::Instance().SetOnClose(menuId, std::move(onClose)), "SetOnClose", menuId);
}

bool SetBackground(const std::string& menuId, bool enabled) {
    return Report(Registry::Instance().SetBackground(menuId, enabled), "SetBackground", menuId);
}

Native::OpenResult Open(const std::string& menuId) {
    const Native::OpenResult result = Native::Open(menuId);
    if (result == Native::OpenResult::Unavailable || result == Native::OpenResult::UnknownMenu) {
        Core::Logger::Error("Menu API: Open '%s' failed: %s", menuId.c_str(), OpenResultName(result));
    } else if (result == Native::OpenResult::Queued) {
        Core::Logger::Info("Menu API: Open '%s' queued until the main menu is available", menuId.c_str());
    }
    return result;
}

bool Close() {
    return Native::Close();
}

std::string CurrentMenuId() {
    return Native::CurrentMenuId();
}

bool SetItemText(const std::string& menuId, const std::string& itemId, const std::string& text) {
    return Live(Registry::Instance().SetItemText(menuId, itemId, text), "SetItemText", menuId, itemId);
}

bool SetItemValue(const std::string& menuId, const std::string& itemId, int value) {
    return Live(Registry::Instance().SetItemValue(menuId, itemId, value), "SetItemValue", menuId, itemId);
}

bool GetItemValue(const std::string& menuId, const std::string& itemId, int& value) {
    return Report(Registry::Instance().GetItemValue(menuId, itemId, value), "GetItemValue", menuId, itemId);
}

bool SetItemEnabled(const std::string& menuId, const std::string& itemId, bool enabled) {
    return Live(Registry::Instance().SetItemEnabled(menuId, itemId, enabled), "SetItemEnabled", menuId, itemId);
}

bool RegisterGraphic(const std::string& graphicId, const std::string& path, const std::string& exportName) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        Core::Logger::Error("Menu API: RegisterGraphic '%s': cannot read '%s'", graphicId.c_str(), path.c_str());
        return false;
    }
    std::vector<std::uint8_t> bytes {std::istreambuf_iterator<char>(file), {}};
    std::string error;
    const auto result = SWF::MenuGraphicAssets::Register(graphicId, std::move(bytes), exportName, error);
    if (result != SWF::MenuGraphicAssets::RegisterResult::Ok) {
        Core::Logger::Error("Menu API: RegisterGraphic '%s' (%s, export '%s'): %s",
                            graphicId.c_str(), path.c_str(), exportName.c_str(), error.c_str());
        return false;
    }
    Core::Logger::Info("Menu API: registered graphic '%s' from %s", graphicId.c_str(), path.c_str());
    return true;
}

const char* OpenResultName(Native::OpenResult result) {
    switch (result) {
    case Native::OpenResult::Opened: return "opened";
    case Native::OpenResult::Queued: return "queued";
    case Native::OpenResult::Unavailable: return "menu API unavailable";
    case Native::OpenResult::UnknownMenu: return "unknown menu";
    }
    return "unknown";
}

} // namespace HookCrashers::UI::Menus::Api

// --- C ABI for native ASI mods (sdk/include/HookCrashers.h) ----------------------
namespace Api = HookCrashers::UI::Menus::Api;
using HookCrashers::UI::Menus::CloseCallback;
using HookCrashers::UI::Menus::ItemCallback;
using HookCrashers::UI::Menus::kMaxOptions;

namespace {
using CMenuCallback = void(__stdcall*)(const char* menuId, const char* itemId, int value, void* userData);

std::string Str(const char* value) {
    return value ? value : "";
}

ItemCallback Bind(const char* menuId, const char* itemId, CMenuCallback callback, void* userData) {
    if (!callback) return {};
    return [menu = Str(menuId), item = Str(itemId), callback, userData](int value) {
        callback(menu.c_str(), item.c_str(), value, userData);
    };
}
} // namespace

extern "C" {

__declspec(dllexport) bool __stdcall HookCrashers_MenuCreate(const char* menuId, const char* title) {
    return Api::CreateMenu(Str(menuId), Str(title));
}

__declspec(dllexport) bool __stdcall HookCrashers_MenuAddButton(const char* menuId, const char* itemId, const char* label,
                                                                CMenuCallback onClick, void* userData) {
    return Api::AddButton(Str(menuId), Str(itemId), Str(label), Bind(menuId, itemId, onClick, userData));
}

__declspec(dllexport) bool __stdcall HookCrashers_MenuAddToggle(const char* menuId, const char* itemId, const char* label,
                                                                bool initial, CMenuCallback onChange, void* userData) {
    return Api::AddToggle(Str(menuId), Str(itemId), Str(label), initial, Bind(menuId, itemId, onChange, userData));
}

__declspec(dllexport) bool __stdcall HookCrashers_MenuAddChoice(const char* menuId, const char* itemId, const char* label,
                                                                const char* const* options, int optionCount,
                                                                int initialIndex, CMenuCallback onChange, void* userData) {
    if (!options || optionCount <= 0 || optionCount > static_cast<int>(kMaxOptions)) return false;
    std::vector<std::string> values;
    for (int i = 0; i < optionCount; ++i) values.push_back(Str(options[i]));
    return Api::AddChoice(Str(menuId), Str(itemId), Str(label), std::move(values), initialIndex,
                          Bind(menuId, itemId, onChange, userData));
}

__declspec(dllexport) bool __stdcall HookCrashers_MenuAddSubmenu(const char* menuId, const char* itemId, const char* label,
                                                                 const char* targetMenuId) {
    return Api::AddSubmenu(Str(menuId), Str(itemId), Str(label), Str(targetMenuId));
}

__declspec(dllexport) bool __stdcall HookCrashers_MenuAddLabel(const char* menuId, const char* itemId, const char* text) {
    return Api::AddLabel(Str(menuId), Str(itemId), Str(text));
}

__declspec(dllexport) bool __stdcall HookCrashers_MenuAddGraphic(const char* menuId, const char* itemId, const char* graphicId,
                                                                 float x, float y) {
    return Api::AddGraphic(Str(menuId), Str(itemId), Str(graphicId), x, y);
}

__declspec(dllexport) bool __stdcall HookCrashers_MenuSetOnClose(const char* menuId, CMenuCallback onClose, void* userData) {
    CloseCallback callback;
    if (onClose) {
        callback = [menu = Str(menuId), onClose, userData]() { onClose(menu.c_str(), "", 0, userData); };
    }
    return Api::SetOnClose(Str(menuId), std::move(callback));
}

__declspec(dllexport) bool __stdcall HookCrashers_MenuSetBackground(const char* menuId, bool enabled) {
    return Api::SetBackground(Str(menuId), enabled);
}

__declspec(dllexport) int __stdcall HookCrashers_MenuOpen(const char* menuId) {
    return static_cast<int>(Api::Open(Str(menuId)));
}

__declspec(dllexport) bool __stdcall HookCrashers_MenuClose() {
    return Api::Close();
}

__declspec(dllexport) bool __stdcall HookCrashers_MenuSetItemText(const char* menuId, const char* itemId, const char* text) {
    return Api::SetItemText(Str(menuId), Str(itemId), Str(text));
}

__declspec(dllexport) bool __stdcall HookCrashers_MenuSetItemValue(const char* menuId, const char* itemId, int value) {
    return Api::SetItemValue(Str(menuId), Str(itemId), value);
}

__declspec(dllexport) int __stdcall HookCrashers_MenuGetItemValue(const char* menuId, const char* itemId) {
    int value = -1;
    return Api::GetItemValue(Str(menuId), Str(itemId), value) ? value : -1;
}

__declspec(dllexport) bool __stdcall HookCrashers_MenuSetItemEnabled(const char* menuId, const char* itemId, bool enabled) {
    return Api::SetItemEnabled(Str(menuId), Str(itemId), enabled);
}

__declspec(dllexport) bool __stdcall HookCrashers_RegisterMenuGraphic(const char* graphicId, const char* swfPath,
                                                                     const char* exportName) {
    return Api::RegisterGraphic(Str(graphicId), Str(swfPath), Str(exportName));
}

} // extern "C"
