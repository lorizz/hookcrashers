#pragma once

namespace HookCrashers::UI::DebugOverlay {

bool Install(bool initiallyVisible = false, unsigned int toggleKey = 0x24);
void Shutdown();
bool IsInstalled();
bool IsVisible();

} // namespace HookCrashers::UI::DebugOverlay
