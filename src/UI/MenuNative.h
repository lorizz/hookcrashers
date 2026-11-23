#pragma once

#include <string>

// Game-side half of the Menu API: builds Menus::Menu definitions into native
// MenuState handlers (slots Game::MenuTable::kFirstModMenuId..) of the main
// menu and routes the game's item callbacks back to the mod callbacks.
// Everything that touches game objects runs on the game thread.
namespace HookCrashers::UI::Menus::Native {

enum class OpenResult : int {
    Opened = 0,       // shown now
    Queued,           // shown when the main menu is next available on the game thread
    Unavailable,      // unsupported build, table not expanded or signatures mismatch
    UnknownMenu,
};

bool Install();
bool IsAvailable();

// Called by the main-menu builder hook on the game thread after the vanilla
// builder returns; resets per-menu native state and shows a queued menu.
void OnMainMenuBuilt(void* mainMenu);

OpenResult Open(const std::string& menuId);
bool Close();                    // closes the current mod menu (like Back)
std::string CurrentMenuId();     // "" when no mod menu is showing

// Live updates (text/value/enabled) are applied on the next game-thread tick
// of the visible menu, or when the menu is next opened.
void MarkDirty(const std::string& menuId);

} // namespace HookCrashers::UI::Menus::Native
