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
#include "SWFDiagnosticsHook.h"
#include "DisplayListDepthHook.h"
#include "../SWF/Helpers/SWFReturnHelper.h"
#include "../SWF/Helpers/SWFArgumentReader.h"
#include "ModLoader.h"
#include <sstream>
#include <iomanip>
#include "GetPlayerObjectHook.h"
#include "DecryptSaveFileHook.h"
#include "BlowfishDecryptHook.h"
#include "../../include/HookCrashers/Public/NativeFunctions.h"
#include "../Save/SaveExpansion.h"
#include "../Localization/LocalizationSystem.h"
#include "../Scripting/ScriptModLoader.h"
#include "../UI/ImGuiOverlay.h"

// Define our aliases for the public types
using HC_SWFArgument = HookCrashers::SWF::Data::SWFArgument;
using HC_SWFReturn = HookCrashers::SWF::Data::SWFReturn;
using HC_SWFFunctionID = HookCrashers::SWF::Data::SWFFunctionID;

namespace HookCrashers {
    namespace Core {
        static Util::Logger& L = Util::Logger::Instance();

        bool HookManager::s_isInitialized = false;
        uintptr_t HookManager::s_moduleBase = 0;
        float HookManager::s_version = 2.5f; // Good practice to specify 'f' for float literals

        void HelloWorldHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
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
                Util::Logger::Instance().Get()->warn("[SWF] GetModInfo called with too few parameters.");
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
                    L.Get()->debug("[SWF] GetModInfo resolved name='{}'.", stringToReturn);
                }
                else if (property == "author") {
                    stringToReturn = mod->author;
                    L.Get()->debug("[SWF] GetModInfo resolved author='{}'.", stringToReturn);
                }
                else if (property == "version") {
                    stringToReturn = mod->version;
                    L.Get()->debug("[SWF] GetModInfo resolved version='{}'.", stringToReturn);
                }
                else {
                    L.Get()->debug("[SWF] GetModInfo received unknown property '{}'.", property);
                }
            }
            else {
                L.Get()->debug("[SWF] GetModInfo found no mod at index {}.", index);
            }

            uint16_t resultStringId = AddCustomString(stringToReturn.c_str());
            L.Get()->debug("[SWF] GetModInfo returned string_id={}.", resultStringId);
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
            L.Get()->info("===== HookCrashers initialization started (module_base=0x{:X}) =====", moduleBase);

            // The rest of this function remains the same, as it's orchestrating
            // the other systems that we have already fixed.
            bool success = true;
            constexpr bool kEnableRuntimeSWFInjectionHooks = false;

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
            if (kEnableRuntimeSWFInjectionHooks) {
                if (!SetupSWFDiagnosticsHook(moduleBase)) {
                    L.Get()->warn("Failed to setup SWF diagnostics hook. Continuing without SWF diagnostics.");
                }

                if (!SetupDisplayListDepthHook(moduleBase)) {
                    L.Get()->warn("Failed to setup display-list depth hook. SVG portrait injected depth ordering may be wrong.");
                }
            }
            else {
                L.Get()->info("[SWFInject] Runtime SWF injection hooks disabled.");
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

            if (!Scripting::ScriptModLoader::Instance().DiscoverAndLoad()) {
                L.Get()->error("Failed to discover script mods!");
                success = false;
            }

            if (!Save::InitializeSaveExpansion()) {
                L.Get()->error("Failed to initialize save expansion subsystem!");
                success = false;
            }

            /*if (!Network::SetupNetworkDiagnosticsHooks(moduleBase)) {
                L.Get()->warn("Failed to install network diagnostic hooks. Continuing without network diagnostics.");
            }*/

            if (!Localization::InitializeLocalizationSystem()) {
                L.Get()->error("Failed to initialize localization subsystem!");
                success = false;
            }

            if (!UI::InitializeOverlay()) {
                L.Get()->warn("Failed to initialize ImGui overlay. Continuing without in-game UI.");
            }

            //L.Get()->debug("Step 6: Setting up RegisterAllSWFFunctions Hook...");
            if (!SetupRegisterAllSWFFunctionsHook(moduleBase)) {
                L.Get()->error("Failed to setup RegisterAllSWFFunctions hook!");
                success = false;
            }

            /*if (!SetupIsFeatureEnabledHook(moduleBase)) {
                L.Get()->error("Failed to setup IsFeatureEnabled hook!");
				success = false;
            }

            if (!SetupGetPlayerObjectHook(moduleBase)) {
                L.Get()->error("Failed to setup GetPlayerObject hook!");
                success = false;
            }*/

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
                L.Get()->info("===== HookCrashers initialization completed successfully =====");
                s_isInitialized = true;

                L.Get()->info("===== Registering loaded mods =====");
                for (const auto& mod : Scripting::ScriptModLoader::Instance().GetLoadedMods()) {
                    ModLoader::RegisterScriptMod(
                        mod.name,
                        mod.author,
                        mod.version,
                        mod.directory,
                        mod.hasMainLua,
                        mod.hasLocalizations,
                        mod.hasManifest,
                        mod.hasIcon,
                        mod.iconPath);
                }
                ModLoader::LoadLegacyBinaryMods();
                L.Get()->info("===== Mod registration finished =====");
            }
            else {
                L.Get()->error("===== HookCrashers initialization failed =====");
                s_isInitialized = false;
            }
            L.Get()->flush();

            return s_isInitialized;
        }
    }
}
