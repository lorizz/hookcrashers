#pragma once

// Adds a third button (save slots) to the main menu row that holds the green
// Options (wrench) button and the red button, inside the MENU movie's
// "buttonchoices" sprite (id 781 in build 0x6A4D42BF):
//
//   before:  [ green, 172 px wide        ][ red ]
//   after:   [ green ][ red ][ save slots ]
//
// The green and teal buttons are recolored copies of the red button shape, the
// save-slot icon is a new white shape in the style of the other row icons. The
// highlight frames follow the selection: frame 4 = green (item 3), frame 5 =
// red (item 4), frame 8 = save slots (item 7, added by MainMenuSlotButton).
// Every original placement is checked first; on any mismatch nothing changes.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace HookCrashers::SWF::SaveSlotButtonAsset {

// Hotspot rectangles of the new row, in main-menu coordinates (pixels).
struct RowLayout {
    float greenLeft, greenRight, redLeft, redRight, slotsLeft, slotsRight;
};
RowLayout Layout();

// Pure transform; `availableBytes` is the physical movie size (tags up to End).
struct Options {
    bool icon = true;       // new floppy icon shape
    bool newShapes = true;  // recolored button shapes (false: reuse the red sprite)
};
bool Build(const std::uint8_t* menu, std::size_t availableBytes, std::vector<std::uint8_t>& output, std::string& error,
           const Options& options = {});

// Game side: patches the MENU scene buffer before it is parsed.
bool PatchScene(void* scene);
bool IsPatched();

} // namespace HookCrashers::SWF::SaveSlotButtonAsset
