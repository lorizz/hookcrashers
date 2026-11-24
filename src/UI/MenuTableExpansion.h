#pragma once

namespace HookCrashers::UI::MenuTableExpansion {

// Grows the main-menu handler table from 46 to Game::MenuTable::kTotalCount
// entries by patching the three immediates of the menu builder. The original
// bytes are verified first; on a mismatch nothing is written.
bool Install();
bool IsInstalled();
void Remove();

} // namespace HookCrashers::UI::MenuTableExpansion
