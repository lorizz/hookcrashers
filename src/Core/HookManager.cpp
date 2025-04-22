#include "HookManager.h"
#include "../Util/Logger.h"
#include "../Native/NativeFunctions.h" // For LoadNatives
#include "../SWF/Custom/CustomFunctions.h"    // For InitializeSystem
#include "../SWF/Override/Overrides.h"         // For InitializeSystem
#include "../SWF/Dispatcher/Dispatcher.h"     // For Initialize
#include "RegisterSWFFunctionHook.h"   // For setup
#include "CallSWFFunctionHook.h"       // For setup
#include "RegisterAllSWFFunctionsHook.h" // For setup

namespace HookCrashers {
    namespace Core {

        bool HookManager::s_isInitialized = false;
        uintptr_t HookManager::s_moduleBase = 0;

        bool HookManager::Initialize(uintptr_t moduleBase) {
            if (s_isInitialized) {
                Util::Logger::Instance().Get()->warn("HookManager::Initialize called again.");
                return true;
            }
            if (moduleBase == 0) {
                Util::Logger::Instance().Get()->error("HookManager::Initialize: Module base is NULL!");
                return false;
            }

            s_moduleBase = moduleBase;
            Util::Logger& L = Util::Logger::Instance(); // Alias
            L.Get()->info("===== HookManager Initialization (Base: 0x{:X}) =====", moduleBase);

            bool success = true;

            // Initialization Order is CRITICAL:
            // 1. Load Native function addresses needed by other systems.
            L.Get()->debug("Step 1: Loading Native Functions... (TODO)");
            /*if (!Native::LoadNatives(moduleBase)) {
                L.Get()->error("Failed to load one or more critical native functions!");
                // Decide if this is fatal. For now, continue but log error.
                success = false; // Mark as failed if natives are essential
            }*/

            // 2. Hook RegisterSWFFunction FIRST to capture 'this' pointer early.
            L.Get()->debug("Step 2: Setting up RegisterSWFFunction Hook...");
            if (!SetupRegisterSWFFunctionHook(moduleBase)) {
                L.Get()->error("Failed to setup RegisterSWFFunction hook!");
                success = false; // This hook is likely essential
            }

            // 3. Hook CallSWFFunction to intercept calls. Dispatcher needs original address.
            L.Get()->debug("Step 3: Setting up CallSWFFunction Hook...");
            if (!SetupCallSWFFunctionHook(moduleBase)) { // Setup also initializes SwfDispatcher
                L.Get()->error("Failed to setup CallSWFFunction hook!");
                success = false; // This hook is essential
            }

            // 4. Initialize SWF Subsystems (Custom Functions, Overrides)
            L.Get()->debug("Step 4a: Initializing Custom SWF Functions System...");
            SWF::Custom::InitializeSystem(); // Registers internal handlers like HelloWorld

            L.Get()->debug("Step 4b: Initializing SWF Override System...");
            SWF::Override::InitializeSystem(moduleBase); // Registers built-in overrides

            // 5. Hook RegisterAllSWFFunctions LAST. Its detour will use the captured 'this'
            //    and the initialized CustomFunctions system to register custom funcs with the game.
            L.Get()->debug("Step 5: Setting up RegisterAllSWFFunctions Hook...");
            if (!SetupRegisterAllSWFFunctionsHook(moduleBase)) {
                L.Get()->error("Failed to setup RegisterAllSWFFunctions hook!");
                success = false; // This is needed to make custom functions callable
            }


            if (success) {
                L.Get()->info("===== HookManager Initialization Successful =====");
                s_isInitialized = true;
            }
            else {
                L.Get()->error("===== HookManager Initialization Failed =====");
                // Consider attempting to Shutdown/cleanup hooks if init failed partially?
                // For simplicity, we'll just mark as not initialized.
                s_isInitialized = false;
            }
            L.Get()->flush();
            return s_isInitialized;
        }

        void HookManager::Shutdown() {
            if (!s_isInitialized) {
                // Util::Logger::Instance().Get()->warn("HookManager::Shutdown called but not initialized.");
                return;
            }
            Util::Logger::Instance().Get()->info("===== HookManager Shutdown =====");

            // Detach hooks in reverse order of attachment (generally safest)
            // Note: Detours automatically handles detaching on process exit if not done explicitly,
            // but explicit detachment is cleaner. Error handling omitted for brevity.

            // TODO: Implement DetourDetach calls if explicit cleanup is desired.
            // Example:
            // if (g_originalRegisterAllFunction) { // Check if hook was successful
            //     DetourTransactionBegin();
            //     DetourUpdateThread(GetCurrentThread());
            //     DetourDetach(&(PVOID&)g_originalRegisterAllFunction, DetouredRegisterAllSWFFunctions);
            //     DetourTransactionCommit();
            //     g_originalRegisterAllFunction = nullptr;
            // }
            // ... detach other hooks ...

            Util::Logger::Instance().Get()->info("Hooks detached (implementation pending)."); // Placeholder message
            Util::Logger::Instance().Get()->flush();

            s_isInitialized = false;
            s_moduleBase = 0;
        }
    }
}