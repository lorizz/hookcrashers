#pragma once

#include "../stdafx.h"

namespace HookCrashers {
    namespace Core {

        // Manages the initialization and shutdown of all hooks
        class HookManager {
        public:
            // Initializes all hooks and subsystems in the correct order.
            // Call this once from the main initialization thread.
            static bool Initialize(uintptr_t moduleBase);

            // Performs cleanup if necessary (detaching hooks, etc.)
            // Call this on DLL detach.
            static void Shutdown();

        private:
            static bool s_isInitialized;
            static uintptr_t s_moduleBase;
        };
    }
}