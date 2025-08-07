#pragma once

#include "../stdafx.h"

namespace HookCrashers {
    namespace Core {
        bool SetupAddStringHook(uintptr_t moduleBase);
        uint16_t AddCustomString(const char* stringToAdd);
    }
}