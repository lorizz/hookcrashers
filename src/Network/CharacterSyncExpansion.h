#pragma once

#include <cstddef>
#include <cstdint>

namespace HookCrashers::Network {

struct CharacterSyncPacketLayout {
    std::uint32_t packetSize;
    std::uint32_t primaryBaseIds;
    std::uint32_t playerMask;
    std::uint32_t selectedWorkshopIds;
    std::uint32_t primaryBaseFlags;
    std::uint32_t selectedWorkshopFlags;
    std::uint32_t secondaryBaseIds;
    std::uint32_t perPlayerWorkshopIds;
    std::uint32_t secondaryBaseFlags;
    std::uint32_t perPlayerWorkshopFlags;
};

class CharacterSyncExpansion {
public:
    static bool Install();
    static bool Remove();

    static constexpr CharacterSyncPacketLayout VanillaLayout();
    static CharacterSyncPacketLayout ExpandedLayout();
    static bool NeedsExpandedLayout();
};

} // namespace HookCrashers::Network
