#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace HookCrashers::SWF::OnlineMenuAssets {
struct SpriteIds { std::uint16_t selector = 0, lobbies = 0; };
// Pure SWF import seam; availableBytes is physical COK6 data, not its legacy
// (often smaller) SWF FileLength. Neither input is modified on failure.
bool Build(const std::uint8_t* menu, std::size_t availableBytes,
           const std::uint8_t* selector, std::size_t selectorBytes,
           const std::uint8_t* lobbies, std::size_t lobbyBytes,
           std::vector<std::uint8_t>& output, SpriteIds& ids);
SpriteIds CurrentIds();
bool PatchScene(void* scene);
}
