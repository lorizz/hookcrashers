#include "LocalizationSystem.h"
#include "LocalizationManager.h"
#include "StringLookupHook.h"
#include "../Config/HookCrashersConfig.h"
#include "../Scripting/ScriptModLoader.h"
#include "../SWF/Custom/CustomFunctions.h"
#include "../SWF/Helpers/SWFArgumentReader.h"
#include "../SWF/Helpers/SWFReturnHelper.h"
#include "../Util/Logger.h"
#include "../../include/HookCrashers/Public/Globals.h"

namespace HookCrashers {
namespace Localization {

using HC_SWFArgument = HookCrashers::SWF::Data::SWFArgument;
using HC_SWFReturn = HookCrashers::SWF::Data::SWFReturn;

static void GetLocalizationHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
    if (paramCount < 1) {
        SWF::Helpers::SWFReturnHelper::SetFailure(swfReturn);
        return;
    }

    const std::string logicalId = SWF::Helpers::SWFArgumentReader::GetString(swfArgs[0]);
    const int returnValue = logicalId.empty() ? -1 : LocalizationManager::getInstance().getNumericId(logicalId);
    SWF::Helpers::SWFReturnHelper::SetIntegerSuccess(swfReturn, returnValue);
}

bool InitializeLocalizationSystem() {
    const auto& settings = Config::HookCrashersConfig::Instance().Get();
    if (!settings.enableCustomLocalizations) {
        Util::Logger::Instance().Get()->info("[Localization] Custom localizations disabled by config.");
        return true;
    }

    auto& manager = LocalizationManager::getInstance();
    manager.Reset();
    if (!manager.initialize(settings.localizationBaseId)) {
        Util::Logger::Instance().Get()->warn("[Localization] Failed to initialize localization manager.");
        return true;
    }

    size_t totalLoaded = 0;
    for (const auto& mod : Scripting::ScriptModLoader::Instance().GetLoadedMods()) {
        if (!mod.localizationPath.empty()) {
            totalLoaded += manager.LoadJsonFile(mod.localizationPath, mod.name);
        }
    }

    if (totalLoaded == 0) {
        Util::Logger::Instance().Get()->warn("[Localization] No locs.json files loaded. Continuing without localization hook.");
        return true;
    }

    if (!SetupStringLookupHook(g_moduleBase)) {
        return false;
    }

    SWF::Custom::Register(static_cast<SWF::Data::SWFFunctionID>(50200), "GetLocalization", GetLocalizationHandler);
    Util::Logger::Instance().Get()->info("[Localization] Localization system initialized.");
    return true;
}

} // namespace Localization
} // namespace HookCrashers
