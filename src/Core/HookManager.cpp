#include "HookManager.h"
#include "../Util/Logger.h"
#include "../SWF/Custom/CustomFunctions.h"
#include "../SWF/Override/Overrides.h"
#include "../SWF/Dispatcher/Dispatcher.h"
#include "IsFeatureEnabledHook.h"
#include "RegisterSWFFunctionHook.h"
#include "CallSWFFunctionHook.h"
#include "UIInputHandlerHook.h"
#include "RegisterAllSWFFunctionsHook.h"
#include "AddStringHook.h"
#include "../SWF/Helpers/SWFReturnHelper.h"
#include "../SWF/Helpers/SWFArgumentReader.h"
#include "ModLoader.h"
#include <sstream>
#include <iomanip>
#include "GetPlayerObjectHook.h"
#include "DecryptSaveFileHook.h"
#include "BlowfishDecryptHook.h"
#include "../../include/HookCrashers/Public/NativeFunctions.h"

// Define our aliases for the public types
using HC_SWFArgument = HookCrashers::SWF::Data::SWFArgument;
using HC_SWFReturn = HookCrashers::SWF::Data::SWFReturn;
using HC_SWFFunctionID = HookCrashers::SWF::Data::SWFFunctionID;

namespace HookCrashers {
    namespace Core {
        static Util::Logger& L = Util::Logger::Instance();

        bool HookManager::s_isInitialized = false;
        uintptr_t HookManager::s_moduleBase = 0;
        float HookManager::s_version = 2.2f; // Good practice to specify 'f' for float literals

        void HelloWorldHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
            // This function is a simple test to ensure the system is working
            Util::Logger::Instance().Get()->info("Hello, world! This is HookCrashers SWF Function Test.");
		}

        // FIXED: Updated function signature to use the public types
        void GetHookCrashersVersionHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
            std::stringstream ss;
            ss << "HC Ver." << std::fixed << std::setprecision(1) << HookManager::GetVersion();
            uint16_t resStringId = AddCustomString(ss.str().c_str());

            if (swfReturn) {
                // FIXED: Use the static helper function to set the return value
                SWF::Helpers::SWFReturnHelper::SetStringSuccess(swfReturn, resStringId);
            }
        }

        void GetModCountHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
            size_t modCount = ModLoader::GetModCount();
            SWF::Helpers::SWFReturnHelper::SetIntegerSuccess(swfReturn, static_cast<int32_t>(modCount));
        }

        void GetModInfoHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
            if (paramCount < 2) {
                Util::Logger::Instance().Get()->warn("GetModInfoHandler: Not enough parameters provided (int index, string propertyName).");
                SWF::Helpers::SWFReturnHelper::SetFailure(swfReturn);
                return;
            }

            int32_t index = SWF::Helpers::SWFArgumentReader::GetInteger(swfArgs[0], -1);
            std::string property = SWF::Helpers::SWFArgumentReader::GetString(swfArgs[1]);

            std::string stringToReturn;
            const ModInfo* mod = ModLoader::GetModInfoByIndex(index);

            if (mod) {
                if (property == "name") {
                    stringToReturn = mod->name;
                    L.Get()->debug("GetModInfoHandler: Setting stringToReturn to name: '{}'", stringToReturn);
                }
                else if (property == "author") {
                    stringToReturn = mod->author;
                    L.Get()->debug("GetModInfoHandler: Setting stringToReturn to author: '{}'", stringToReturn);
                }
                else if (property == "version") {
                    stringToReturn = mod->version;
                    L.Get()->debug("GetModInfoHandler: Setting stringToReturn to version: '{}'", stringToReturn);
                }
                else {
                    L.Get()->debug("GetModInfoHandler: Unknown property '{}' requested", property);
                }
            }
            else {
                L.Get()->debug("GetModInfoHandler: No mod found at index {}", index);
            }

            L.Get()->debug("GetModInfoHandler: Final stringToReturn: '{}'", stringToReturn);
            uint16_t resultStringId = AddCustomString(stringToReturn.c_str());
            L.Get()->debug("GetModInfoHandler: AddCustomString returned ID: {}", resultStringId);
            SWF::Helpers::SWFReturnHelper::SetStringSuccess(swfReturn, resultStringId);
        }

        bool HookManager::Initialize(uintptr_t moduleBase) {
            if (s_isInitialized) {
                //Util::Logger::Instance().Get()->warn("HookManager::Initialize called again.");
                return true;
            }
            if (moduleBase == 0) {
                //Util::Logger::Instance().Get()->error("HookManager::Initialize: Module base is NULL!");
                return false;
            }

            s_moduleBase = moduleBase;
            Util::Logger& L = Util::Logger::Instance();
            L.Get()->info("===== HookManager Initialization (Base: 0x{:X}) =====", moduleBase);

            // The rest of this function remains the same, as it's orchestrating
            // the other systems that we have already fixed.
            bool success = true;

            //L.Get()->debug("Step 1: Loading Native Functions... (TODO)");
            if (!Native::LoadNatives(moduleBase)) {
                L.Get()->error("Failed to load native functions!");
                success = false;
			}

            //L.Get()->debug("Step 2: Setting up RegisterSWFFunction Hook...");
            if (!SetupRegisterSWFFunctionHook(moduleBase)) {
                L.Get()->error("Failed to setup RegisterSWFFunction hook!");
                success = false;
            }

            //L.Get()->debug("Step 3: Setting up AddString Hook...");
            if (!SetupAddStringHook(moduleBase)) {
                L.Get()->error("Failed to setup AddString hook!");
                success = false;
            }

            //L.Get()->debug("Step 4: Setting up CallSWFFunction Hook...");
            if (!SetupCallSWFFunctionHook(moduleBase)) {
                L.Get()->error("Failed to setup CallSWFFunction hook!");
                success = false;
            }

            if (!SetupUIInputHandlerHook(moduleBase)) {
                L.Get()->error("Failed to setup UIInputHandler Hook!");
                success = false;
            }

            if (!SetupBlowfishDecryptHook(moduleBase)) {
				L.Get()->error("Failed to setup BlowfishDecrypt Hook!");
                success = false;
            }

            //L.Get()->debug("Step 5a: Initializing Custom SWF Functions System...");
            SWF::Custom::InitializeSystem();

            //L.Get()->debug("Step 5b: Initializing SWF Override System...");
            SWF::Override::InitializeSystem(moduleBase);

            //L.Get()->debug("Step 6: Setting up RegisterAllSWFFunctions Hook...");
            if (!SetupRegisterAllSWFFunctionsHook(moduleBase)) {
                L.Get()->error("Failed to setup RegisterAllSWFFunctions hook!");
                success = false;
            }

            if (!SetupIsFeatureEnabledHook(moduleBase)) {
                L.Get()->error("Failed to setup IsFeatureEnabled hook!");
				success = false;
            }

            if (!SetupGetPlayerObjectHook(moduleBase)) {
                L.Get()->error("Failed to setup GetPlayerObject hook!");
                success = false;
            }

            if (!SetupDecryptSaveFileHook(moduleBase)) {
                L.Get()->error("Failed to setup DecryptSaveFile hook!");
				success = false;
            }

            //L.Get()->debug("Step 7: Register custom functions...");
            // This call will now work because GetHookCrashersVersion has the correct signature
            SWF::Custom::Register(HC_SWFFunctionID::HelloWorld, "HelloWorld", HelloWorldHandler);
            SWF::Custom::Register(HC_SWFFunctionID::GetHookCrashersVersion, "GetHookCrashersVersion", GetHookCrashersVersionHandler);
            SWF::Custom::Register(HC_SWFFunctionID::GetModCount, "GetModCount", GetModCountHandler);
            SWF::Custom::Register(HC_SWFFunctionID::GetModInfo, "GetModInfo", GetModInfoHandler);

            if (success) {
                L.Get()->info("===== HookManager Initialization Successful =====");
                s_isInitialized = true;

                L.Get()->info("===== Starting Mod Loader =====");
                ModLoader::LoadAllMods();
                L.Get()->info("===== Mod Loader Finished =====");
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