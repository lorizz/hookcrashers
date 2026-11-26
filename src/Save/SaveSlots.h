#pragma once

// Save slots (up to 10) for modded play. Each slot is its own Steam Cloud save
// (hc_slotN.dat + .bak) with a metadata file (hc_slotN.meta). The vanilla
// cc_save.dat is only ever read (import), never written.
//
// The native CC_ReadSaveFile / CC_WriteSaveFile are detoured: before every read
// the active slot is brought to the current character layout (Layout::Remap),
// after every write its metadata (last played, mods, versions) is refreshed.
// Switching slots rewrites the redirected filename and re-runs the native read
// on the game thread, verified against the expected bytes.
#include "SlotMeta.h"

#include <string>
#include <vector>

namespace HookCrashers::Save::Slots {

// Bootstrap thread, after mods are loaded. Installs the filename redirect and
// the save detours; slot state itself is loaded on the first native read.
bool Install(const std::string& legacyManifestName);
bool IsInstalled();
// True once Steam storage was reachable and the slot list was loaded.
bool IsReady();

struct SlotInfo {
    int index = 0;
    bool used = false;
    bool active = false;
    SlotMeta meta;
    std::vector<ModComparison> mods;
    ModSummary summary;
};

// Game thread.
std::vector<SlotInfo> List();
int ActiveSlot();
bool VanillaSaveAvailable();
std::vector<ModEntry> CurrentMods();

bool Activate(int index, std::string& error);            // load (or create) the slot now
bool ImportVanilla(int index, std::string& error);       // copy cc_save.dat into an empty slot and load it
bool Delete(int index, std::string& error);              // moves the files to hc_trash_slotN.*, never the active slot

// Called after the slot list changes (menu refresh). Runs on the game thread.
void SetChangedCallback(void (*callback)());

} // namespace HookCrashers::Save::Slots
