#include <HookCrashers.h>
#include <array>
#include <cstdio>

namespace {
struct PreviousPlayer {
    bool valid = false;
    double character = 0, port = 0, level = 0;
};
std::array<PreviousPlayer, 4> previous;

void Update() {
    for (unsigned slot = 1; slot <= 4; ++slot) {
        const auto data = HookCrashers::GetCurrentCharacterData(slot);
        auto& last = previous[slot - 1];
        using namespace HookCrashers;
        if (data.result != CharacterDataResult::Ok ||
            data.source != CharacterDataSource::LivePlayer ||
            data.state != CharacterDataState::InLevel ||
            !HasCharacterField(data, CurrentCharacterField::Level) ||
            !HasCharacterField(data, CurrentCharacterField::CharacterId) ||
            !HasCharacterField(data, CurrentCharacterField::SavePort)) {
            last.valid = false;
            continue;
        }
        // Reset on joins/character changes/loading; an initial read is no level-up.
        if (last.valid && last.character == data.characterId && last.port == data.savePort && data.level > last.level) {
            // Queue an achievement check here; perform network/disk work elsewhere.
            char message[128];
            std::snprintf(message, sizeof(message), "Player %u level increased: %.0f -> %.0f", slot, last.level, data.level);
            HookCrashers::LogInfo(message);
        }
        last = {true, data.characterId, data.savePort, data.level};
    }
}
void __stdcall OnUpdate() { Update(); }
}

HOOKCRASHERS_MOD_EXPORT bool __stdcall HookCrashersModInit() {
    return HookCrashers::RegisterUpdateCallback(OnUpdate);
}
HOOKCRASHERS_MOD_EXPORT void __stdcall HookCrashersModShutdown() {
    // Successful removal waits for any executing OnUpdate before returning.
    HookCrashers::UnregisterUpdateCallback(OnUpdate);
}
