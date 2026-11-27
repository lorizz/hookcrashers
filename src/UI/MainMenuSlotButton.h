#pragma once

// Native side of the save-slot button in the main menu row (see
// SWF/SaveSlotButtonAsset.h for the graphics). The main menu (MenuState 23,
// built by 0x1451C0) has seven hidden text items driven by the "buttonchoices"
// clip, which shows frame selected+1. Item 3 is the green Options button and
// item 4 the red one, side by side (item 7 is the version text); this adds
// item 8 to their right:
//   - a hidden text item whose confirm opens the save-slot screen,
//   - a mouse hotspot, and narrower hotspots for items 3 and 4,
//   - a pre-update that moves left/right through 3 -> 4 -> 8 -> 3.
namespace HookCrashers::UI::MainMenuSlotButton {

// Game thread, right after the native main-menu builder returns.
void OnMainMenuBuilt(void* mainMenu);

} // namespace HookCrashers::UI::MainMenuSlotButton
