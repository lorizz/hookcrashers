#pragma once

// Character-slot layout of a decrypted Castle Crashers save, platform independent.
//
//   0x000  global data (0x40)
//   0x040  32 base characters       (0x30 each)
//   0x640  N addon characters        (HookCrashers, in registration order)
//   ...    10 workshop characters
//   ...    keybinds (0x2C)
//   end-4  checksum
//
// Vanilla is N = 0 (0x850 bytes). Remap() moves a save from one addon list to
// another by character key, so adding, removing or reordering mods never shifts
// another character's progress. Characters that are no longer installed go to
// a stash (kept in the slot metadata) and come back when the mod returns.
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace HookCrashers::Save::Layout {

constexpr std::size_t kGlobalSize = 0x40;
constexpr std::size_t kCharacterSize = 0x30;
constexpr std::size_t kBaseCharacters = 32;
constexpr std::size_t kWorkshopCharacters = 10;
constexpr std::size_t kVanillaSize = 0x850;
constexpr std::size_t kTailSize = 0x2C + 4;   // keybinds + checksum
constexpr std::size_t kFirstAddonOffset = kGlobalSize + kBaseCharacters * kCharacterSize; // 0x640

using CharacterBytes = std::array<std::uint8_t, kCharacterSize>;
using Stash = std::map<std::string, CharacterBytes>;
// Fills the default data of addon `addonIndex` (0-based) of the new layout.
using InitAddon = std::function<void(std::size_t addonIndex, std::uint8_t* character)>;

constexpr std::size_t SizeFor(std::size_t addonCount) {
    return kVanillaSize + addonCount * kCharacterSize;
}

// Addon count implied by a save size, or -1 if the size is not a valid layout.
int AddonCountForSize(std::size_t size);

// Stable keys for an addon list: the character name, with "#2", "#3"... appended
// to repeated names so each slot has its own key.
std::vector<std::string> MakeKeys(const std::vector<std::string>& names);

struct RemapReport {
    std::size_t kept = 0;       // addon characters found in the old layout
    std::size_t restored = 0;   // taken back from the stash
    std::size_t created = 0;    // new characters (default data)
    std::size_t stashed = 0;    // old characters no longer installed
};

// `plain` must be a decrypted save with plain.size() == SizeFor(oldKeys.size()).
// On success `out` holds the remapped save with a fresh checksum.
bool Remap(const std::vector<std::uint8_t>& plain,
           const std::vector<std::string>& oldKeys,
           const std::vector<std::string>& newKeys,
           Stash& stash,
           const InitAddon& initAddon,
           std::vector<std::uint8_t>& out,
           RemapReport& report,
           std::string& error);

bool SameLayout(const std::vector<std::string>& a, const std::vector<std::string>& b);

} // namespace HookCrashers::Save::Layout
