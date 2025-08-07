#pragma once
#include <cstdint>

namespace HookCrashers {
    namespace Core {
        bool SetupChangeMenuStateHook(uintptr_t moduleBase);
    }
}