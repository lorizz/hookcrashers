#pragma once

// Save-slot metadata (hc_slotN.meta), platform independent.
//
// Plain UTF-8 text, one "key=value" per line, so it survives Steam Cloud and
// can be read by hand:
//
//   hcslot=1
//   name=Slot 1
//   created=1764000000
//   played=1764100000
//   hc=5.3.0
//   char=Test Knight          (addon order of the save file, repeated)
//   mod=Test Mod\t1.0.0       (mods of the last session that wrote it, repeated)
//   stash=Old Knight\t<96 hex> (characters of mods no longer installed)
#include "SaveLayout.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace HookCrashers::Save::Slots {

constexpr int kMaxSlots = 10;
constexpr std::size_t kMaxNameBytes = 48;

struct ModEntry {
    std::string name;
    std::string version;
    bool operator==(const ModEntry& other) const { return name == other.name && version == other.version; }
};

struct SlotMeta {
    int index = 0;                       // 1..kMaxSlots
    std::string name;
    std::int64_t created = 0;            // unix seconds
    std::int64_t played = 0;             // last write by the game, unix seconds
    std::string hcVersion;
    bool layoutKnown = false;            // false: character list inferred from the file size
    std::vector<std::string> characters; // addon keys in save order
    std::vector<ModEntry> mods;
    Layout::Stash stash;
};

std::string Serialize(const SlotMeta& meta);
bool Parse(const std::string& text, SlotMeta& meta);

std::string SaveFileName(int index);   // hc_slotN.dat
std::string BackupFileName(int index); // hc_slotN.dat.bak
std::string MetaFileName(int index);   // hc_slotN.meta

enum class ModState {
    Match,    // green: installed, same version
    Outdated, // orange: installed, different version
    Missing,  // red: used by the slot, not installed
    Added,    // installed now, never used by the slot
};

struct ModComparison {
    std::string name;
    std::string savedVersion;
    std::string currentVersion;
    ModState state = ModState::Match;
};

struct ModSummary {
    int match = 0;
    int outdated = 0;
    int missing = 0;
    int added = 0;
};

std::vector<ModComparison> CompareMods(const std::vector<ModEntry>& saved, const std::vector<ModEntry>& current);
ModSummary Summarize(const std::vector<ModComparison>& comparison);

// UTF-8 safe truncation used for names coming from files or the user.
std::string ClampUtf8(const std::string& text, std::size_t maxBytes);

} // namespace HookCrashers::Save::Slots
