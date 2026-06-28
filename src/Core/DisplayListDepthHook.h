#pragma once

#include <cstdint>

namespace HookCrashers::Core {
    bool SetupDisplayListDepthHook(uintptr_t moduleBase);
}