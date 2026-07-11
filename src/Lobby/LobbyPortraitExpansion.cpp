#include "LobbyPortraitExpansion.h"

#include "../Characters/CharacterRegistry.h"
#include "../Core/Logger.h"

namespace HookCrashers::Lobby {
bool LobbyPortraitExpansion::Install() {
    const auto& registry = Characters::Registry::Instance();
    const int classicAddonCount = registry.ClassicAddonCount();
    if (classicAddonCount <= 0) {
        Core::Logger::Info("LobbyPortraitExpansion skipped: no classic-visible addons");
        return true;
    }

    // Classic addon portraits are written into the vanilla empty gap between classic
    // and fresh portraits in lobby.cok6, so the native fresh-frame subtract remains
    // vanilla. Fresh/workshop shifting is handled by the roster/workshop boundary patches.
    Core::Logger::Info("LobbyPortraitExpansion native subtract unchanged: classicAddons=%d use classic frame gap",
                       classicAddonCount);
    return true;
}

} // namespace HookCrashers::Lobby
