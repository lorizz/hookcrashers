#include "SaveExpansion.h"
#include "CharacterConfig.h"
#include "SavePatches.h"
#include "PainterConfigHook.h"
#include "GenerateDefaultCharacterDataHook.h"
#include "RebuildCharacterSlotTableHook.h"
#include "SyncCharacterListFromSaveHook.h"
#include "../SWF/Custom/CustomFunctions.h"
#include "../SWF/Helpers/SWFArgumentReader.h"
#include "../SWF/Helpers/SWFReturnHelper.h"
#include "../Util/Logger.h"

namespace HookCrashers {
namespace Save {

using HC_SWFArgument = HookCrashers::SWF::Data::SWFArgument;
using HC_SWFReturn = HookCrashers::SWF::Data::SWFReturn;

static void GetCustomSaveDataHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
    if (paramCount < 1) {
        SWF::Helpers::SWFReturnHelper::SetFailure(swfReturn);
        return;
    }

    const std::string property = SWF::Helpers::SWFArgumentReader::GetString(swfArgs[0]);
    const int addonCount = CharacterConfig::Instance().GetAddonCount();
    int returnValue = -1;

    if (property == "char_offset") returnValue = 64;
    else if (property == "char_size") returnValue = 48;
    else if (property == "num_items") returnValue = 128;
    else if (property == "num_animals") returnValue = 32;
    else if (property == "num_levels") returnValue = 64;
    else if (property == "num_relics") returnValue = 8;
    else if (property == "num_items_expansion") returnValue = 64;
    else if (property == "num_characters_legacy") returnValue = 31;
    else if (property == "num_characters_noaddons") returnValue = 32 + addonCount;
    else if (property == "num_characters_safe") returnValue = 42 + addonCount;
    else if (property == "num_characters_addons") returnValue = 10;
    else if (property == "num_characters") returnValue = 72 + addonCount;

    Util::Logger::Instance().Get()->debug("[Save] GetCustomSaveData '{}' -> {}", property, returnValue);
    SWF::Helpers::SWFReturnHelper::SetIntegerSuccess(swfReturn, returnValue);
}

bool InitializeSaveExpansion() {
    CharacterConfig::Instance().LoadFromHookCrashersConfig();
    const auto& addons = CharacterConfig::Instance().GetAddons();
    Util::Logger::Instance().Get()->info("[Save] Initializing save expansion for {} addon characters.", addons.size());
    for (const auto& addon : addons) {
        Util::Logger::Instance().Get()->info(
            "[Save] Addon character active id='{}' weapon={} pet={} initially_unlocked={} fresh_only={}.",
            addon.id,
            addon.weapon,
            addon.animal,
            addon.unlocked,
            addon.freshOnly);
    }

    bool success = true;
    success = ApplySaveExpansionPatches() && success;
    success = SetupPainterConfigHook() && success;
    success = SetupGenerateDefaultCharacterDataHook() && success;
    success = SetupRebuildCharacterSlotTableHook() && success;
    success = SetupSyncCharacterListFromSaveHook() && success;

    SWF::Custom::Register(static_cast<SWF::Data::SWFFunctionID>(50100), "GetCustomSaveData", GetCustomSaveDataHandler);
    const int addonCount = CharacterConfig::Instance().GetAddonCount();
    const int expandedBufferSize = BASE_SAVE_SIZE + (CHAR_DATA_SIZE * addonCount);
    const int expandedCapacity = BASE_SAVE_CAPACITY + (CHAR_DATA_SIZE * addonCount);
    Util::Logger::Instance().Get()->info(
        "[Save] Save expansion initialization finished success={} addon_count={} expanded_buffer_size={} expanded_capacity={} expanded_character_slots={} safe_character_slots={}.",
        success,
        addonCount,
        expandedBufferSize,
        expandedCapacity,
        CharacterConfig::Instance().GetExpandedCharacterCount(),
        CharacterConfig::Instance().GetExpandedSafeCharacterCount());
    return success;
}

} // namespace Save
} // namespace HookCrashers
