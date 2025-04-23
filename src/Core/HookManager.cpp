#include "HookManager.h"
#include "../Util/Logger.h"
#include "../Native/NativeFunctions.h"
#include "../SWF/Custom/CustomFunctions.h"
#include "../SWF/Override/Overrides.h"
#include "../SWF/Dispatcher/Dispatcher.h"
#include "RegisterSWFFunctionHook.h"
#include "CallSWFFunctionHook.h"
#include "RegisterAllSWFFunctionsHook.h"
#include "AddStringHook.h"

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
            Util::Logger& L = Util::Logger::Instance();
            L.Get()->info("===== HookManager Initialization (Base: 0x{:X}) =====", moduleBase);

            bool success = true;

            L.Get()->debug("Step 1: Loading Native Functions... (TODO)");
            /*if (!Native::LoadNatives(moduleBase)) {
                L.Get()->error("Failed to load one or more critical native functions!");
                // Decide if this is fatal. For now, continue but log error.
                success = false; // Mark as failed if natives are essential
            }*/

            L.Get()->debug("Step 2: Setting up RegisterSWFFunction Hook...");
            if (!SetupRegisterSWFFunctionHook(moduleBase)) {
                L.Get()->error("Failed to setup RegisterSWFFunction hook!");
                success = false;
            }

            L.Get()->debug("Step 3: Setting up AddString Hook...");
            if (!SetupAddStringHook(moduleBase)) {
                L.Get()->error("Failed to setup AddString hook!");
                success = false;
            }

            L.Get()->debug("Step 4: Setting up CallSWFFunction Hook...");
            if (!SetupCallSWFFunctionHook(moduleBase)) {
                L.Get()->error("Failed to setup CallSWFFunction hook!");
                success = false;
            }

            L.Get()->debug("Step 5a: Initializing Custom SWF Functions System...");
            SWF::Custom::InitializeSystem();

            L.Get()->debug("Step 5b: Initializing SWF Override System...");
            SWF::Override::InitializeSystem(moduleBase);

            L.Get()->debug("Step 6: Setting up RegisterAllSWFFunctions Hook...");
            if (!SetupRegisterAllSWFFunctionsHook(moduleBase)) {
                L.Get()->error("Failed to setup RegisterAllSWFFunctions hook!");
                success = false;
            }


            if (success) {
                L.Get()->info("===== HookManager Initialization Successful =====");
                s_isInitialized = true;
            }
            else {
                L.Get()->error("===== HookManager Initialization Failed =====");
                s_isInitialized = false;
            }
            L.Get()->flush();
            return s_isInitialized;
        }
    }
}