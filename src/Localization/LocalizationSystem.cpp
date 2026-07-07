#include "LocalizationSystem.h"
#include "LocalizationManager.h"
#include "StringLookupHook.h"
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

constexpr int kBaseCustomLocalizationId = 5000;

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
    auto& manager = LocalizationManager::getInstance();
    manager.Reset();
    if (!manager.initialize(kBaseCustomLocalizationId)) {
        Util::Logger::Instance().Get()->warn("[Localization] Failed to initialize localization manager.");
        return true;
    }

    size_t totalLoaded = 0;
    if (manager.RegisterBuiltInString("hookcrashers.matchmaking.no_lobbies", {
        "NO MODDED LOBBIES FOUND",
        "KEINE MODDED-LOBBYS GEFUNDEN",
        "AUCUN SALON MODDE TROUVE",
        "NO SE ENCONTRARON SALAS MODIFICADAS",
        "NESSUNA LOBBY MODDATA TROVATA",
        u8"\u672A\u627E\u5230\u6A21\u7EC4\u5927\u5385",
        u8"\uBAA8\uB4DC \uB85C\uBE44\uB97C \uCC3E\uC744 \uC218 \uC5C6\uC74C",
        u8"\u672A\u627E\u5230\u6A21\u7D44\u5927\u5EF3",
        "NENHUMA SALA MODIFICADA ENCONTRADA",
        u8"MOD\u30ED\u30D3\u30FC\u304C\u898B\u3064\u304B\u308A\u307E\u305B\u3093",
        u8"\u041C\u041E\u0414\u0414\u0418\u041D\u0413-\u041B\u041E\u0411\u0411\u0418 \u041D\u0415 \u041D\u0410\u0419\u0414\u0415\u041D\u042B",
    })) {
        ++totalLoaded;
    }

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
    Util::Logger::Instance().Get()->debug("[Localization] Localization system initialized.");
    return true;
}

} // namespace Localization
} // namespace HookCrashers
