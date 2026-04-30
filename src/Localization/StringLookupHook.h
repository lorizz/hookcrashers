#pragma once
#include <cstdint>

namespace HookCrashers::Localization {
    bool SetupStringLookupHook(uintptr_t moduleBase);
}