#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

namespace HookCrashers::UI::SplashScreen {

void Show(HMODULE moduleHandle, DWORD durationMs);

} // namespace HookCrashers::UI::SplashScreen
