#pragma once
#include <cstdint>

namespace HookCrashers::Network {
enum class LobbyGameMode : std::uint8_t { Classic = 0, Fresh = 1, Unknown = 0xFF };
constexpr bool IsKnownLobbyMode(LobbyGameMode mode) {
    return mode == LobbyGameMode::Classic || mode == LobbyGameMode::Fresh;
}
// Same authority predicate as the original game's character-sync receiver.
constexpr bool ShouldAdoptHostMode(bool syncEnabled,bool authoritativePeer,bool localHost) {
    return syncEnabled && authoritativePeer && !localHost;
}
inline const char* LobbyGameModeName(LobbyGameMode mode) {
    return mode == LobbyGameMode::Classic ? "Classic" : mode == LobbyGameMode::Fresh ? "Fresh" : "Unknown";
}
}
