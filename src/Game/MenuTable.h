#pragma once

#include <cstddef>
#include <cstdint>

// Layout of the main-menu handler table built by the menu builder (RVA 0x1442F0)
// in the supported build. The builder allocates `4 + count * 0x20C` bytes, stores
// the count in the first dword and constructs `count` MenuState objects after it.
namespace HookCrashers::Game::MenuTable {

constexpr std::uint16_t kVanillaCount = 46;            // native ids 0..45
constexpr std::uint16_t kOnlineCompatibilityId = 46;   // HookCrashers online selector
constexpr std::uint16_t kPlayerLobbiesId = 47;         // HookCrashers lobby browser
constexpr std::uint16_t kFirstModMenuId = 48;          // Menu API slots start here
constexpr std::uint16_t kModMenuSlotCount = 16;
constexpr std::uint16_t kTotalCount = kFirstModMenuId + kModMenuSlotCount; // 64

constexpr std::uint16_t kConfirmationMenuId = 3;       // native "bookbg" Yes/No menu
constexpr std::uint16_t kMainMenuId = 23;

constexpr std::size_t kHandlerSize = 0x20C;

static_assert(kTotalCount <= 127, "menu count is pushed as a signed imm8 by the builder");

} // namespace HookCrashers::Game::MenuTable
