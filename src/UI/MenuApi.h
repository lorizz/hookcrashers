#pragma once

#include "MenuModel.h"
#include "MenuNative.h"

#include <string>
#include <vector>

// Facade shared by the C exports (sdk/include/HookCrashers.h) and the Lua
// bindings (HC.Menu). Every function logs the reason when it returns false.
namespace HookCrashers::UI::Menus::Api {

bool CreateMenu(const std::string& menuId, const std::string& title);
bool AddButton(const std::string& menuId, const std::string& itemId, const std::string& label, ItemCallback onClick);
bool AddToggle(const std::string& menuId, const std::string& itemId, const std::string& label, bool initial,
               ItemCallback onChange, std::vector<std::string> labels = {});
bool AddChoice(const std::string& menuId, const std::string& itemId, const std::string& label,
               std::vector<std::string> options, int initialIndex, ItemCallback onChange);
bool AddSubmenu(const std::string& menuId, const std::string& itemId, const std::string& label, const std::string& targetMenuId);
bool AddLabel(const std::string& menuId, const std::string& itemId, const std::string& text);
bool AddGraphic(const std::string& menuId, const std::string& itemId, const std::string& graphicId, float x, float y);
bool SetOnClose(const std::string& menuId, CloseCallback onClose);
bool SetBackground(const std::string& menuId, bool enabled);

Native::OpenResult Open(const std::string& menuId);
bool Close();
std::string CurrentMenuId();

bool SetItemText(const std::string& menuId, const std::string& itemId, const std::string& text);
bool SetItemOptions(const std::string& menuId, const std::string& itemId, std::vector<std::string> options);
bool SetItemValue(const std::string& menuId, const std::string& itemId, int value);
bool GetItemValue(const std::string& menuId, const std::string& itemId, int& value);
bool SetItemEnabled(const std::string& menuId, const std::string& itemId, bool enabled);

// `path` is read immediately; graphics must be registered before MENU loads.
bool RegisterGraphic(const std::string& graphicId, const std::string& path, const std::string& exportName);

const char* OpenResultName(Native::OpenResult result);

} // namespace HookCrashers::UI::Menus::Api
