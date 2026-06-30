#include "CharacterConfig.h"
#include "../Util/Logger.h"

namespace HookCrashers {
namespace Save {

CharacterConfig& CharacterConfig::Instance() {
    static CharacterConfig instance;
    return instance;
}

void CharacterConfig::LoadFromHookCrashersConfig() {
    Util::Logger::Instance().Get()->debug(
        "[Save] Character registry ready: addons={} base_slots={} expanded_slots={} safe_slots={}.",
        m_addons.size(),
        BASE_CHAR_COUNT,
        GetExpandedCharacterCount(),
        GetExpandedSafeCharacterCount());
}

void CharacterConfig::RegisterCharacter(const Config::AddonCharacterDef& def) {
    m_addons.push_back(def);
    Util::Logger::Instance().Get()->info(
        "[Save] Registered addon character '{}' weapon={} pet={} initially_unlocked={} fresh_only={} portrait_classic='{}' portrait_fresh='{}' registry_size={}.",
        def.id, def.weapon, def.animal, def.unlocked, def.freshOnly, def.portraitClassicPath, def.portraitFreshPath, m_addons.size());
}

void CharacterConfig::ClearRegisteredCharacters() {
    Util::Logger::Instance().Get()->debug("[Save] Cleared addon character registry.");
    m_addons.clear();
}

} // namespace Save
} // namespace HookCrashers
