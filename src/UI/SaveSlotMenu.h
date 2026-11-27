#pragma once

// Save-slot screen shown before the main menu on every launch with mods:
// a left/right selector over the 10 slots, details of the selected slot and
// Play / Mod Info / Import vanilla save / Delete. Built on the Menu API.
namespace HookCrashers::UI::SaveSlotMenu {

// Bootstrap thread, after Save::Slots::Install. Registers the menus and queues
// the slot screen for the first main menu.
bool Install();

} // namespace HookCrashers::UI::SaveSlotMenu
