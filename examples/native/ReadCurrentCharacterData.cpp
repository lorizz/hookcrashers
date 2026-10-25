#include <HookCrashers.h>

// Call from the consuming integration's update/timer. No raw game pointers are
// retained, and an unavailable/loading/HUD-only snapshot does not trigger checks.
bool TryReadAchievementHealth(std::uint32_t playerSlot, double& health,
                              std::uint32_t& rawCharacterId) {
    const auto data = HookCrashers::GetCurrentCharacterData(playerSlot);
    if (data.result != HookCrashers::CharacterDataResult::Ok ||
        data.source != HookCrashers::CharacterDataSource::LivePlayer ||
        data.state != HookCrashers::CharacterDataState::InLevel ||
        !HookCrashers::HasCharacterField(data, HookCrashers::CurrentCharacterField::Health) ||
        !HookCrashers::HasCharacterField(data, HookCrashers::CurrentCharacterField::CharacterId)) return false;
    health = data.health;
    rawCharacterId = static_cast<std::uint32_t>(data.characterId);
    return true;
}
