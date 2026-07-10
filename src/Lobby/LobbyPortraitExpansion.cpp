#include "LobbyPortraitExpansion.h"

#include "../Characters/CharacterRegistry.h"
#include "../Core/Memory.h"
#include <cstdint>

namespace HookCrashers::Lobby {
namespace {

constexpr std::uintptr_t kAttachSkinGraphicFreshPortraitSubtractImmediateRva = 0x8D34C;
constexpr int kVanillaFreshPortraitFrameStart = 102;

} // namespace

bool LobbyPortraitExpansion::Install() {
    const auto& registry = Characters::Registry::Instance();
    const int classicAddonCount = registry.ClassicAddonCount();
    if (classicAddonCount <= 0) {
        return true;
    }

    const int freshFrameStart = kVanillaFreshPortraitFrameStart + classicAddonCount;
    const std::uint16_t subtractImmediate = static_cast<std::uint16_t>(0x10000 - freshFrameStart);

    // CC_LobbySkinManagerAttachSkinGraphic(this, 36 / 0x24) maps fresh portrait frames
    // by subtracting the hardcoded first fresh frame. Classic/base addons inserted before
    // fresh shift that frame start, so only this lobby portrait path needs a dynamic fix.
    return Core::Memory::WriteU16(kAttachSkinGraphicFreshPortraitSubtractImmediateRva, subtractImmediate);
}

} // namespace HookCrashers::Lobby
