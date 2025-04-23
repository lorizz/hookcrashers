#pragma once

#include "../stdafx.h"

namespace HookCrashers {
    namespace Core {
        class HookManager {
        public:
            static bool Initialize(uintptr_t moduleBase);

        private:
            static bool s_isInitialized;
            static uintptr_t s_moduleBase;
        };
    }
}