#pragma once

#include "../stdafx.h"

namespace HookCrashers {
    namespace Core {
        class HookManager {
        public:
            static bool Initialize(uintptr_t moduleBase);
            static float GetVersion() { return s_version; }
            static bool IsInitialized() { return s_isInitialized; }

        private:
            static bool s_isInitialized;
            static uintptr_t s_moduleBase;
            static float s_version;
        };
    }
}