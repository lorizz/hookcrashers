#pragma once

namespace HookCrashers::Core::GraphicsQuality {
// Install before the executable entry point reads video settings.
bool Install();
bool IsInstalled();
const char* FailureReason();
void Shutdown();
}
