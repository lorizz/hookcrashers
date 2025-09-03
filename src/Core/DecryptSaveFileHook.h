#pragma once
#include "../stdafx.h"
#include "../../include/HookCrashersAPI.h"

namespace HookCrashers {
    namespace Core {
        bool SetupDecryptSaveFileHook(uintptr_t moduleBase);
    }
}