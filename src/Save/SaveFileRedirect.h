#pragma once

#include <string>

// Points every native reference to "cc_save.dat", "cc_save.dat.bak" and the PBP
// config filename at HookCrashers-owned buffers, so the vanilla files are never
// opened while mods are loaded.
namespace HookCrashers::Save::SaveFileRedirect {

bool Install(const std::string& mainName, const std::string& backupName, const std::string& pbpName);
// Game thread only, between native save reads/writes (hot slot switch).
bool SetSlotFileNames(const std::string& mainName, const std::string& backupName);

const std::string& SaveFileName();
const std::string& BackupFileName();
const std::string& PbpConfigFileName();

// Names used before save slots (5.2.0 and older): hc_<first manifest>.dat.
std::string LegacyManifestSaveName(const std::string& manifestName);
std::string LegacyManifestPbpName(const std::string& manifestName);

} // namespace HookCrashers::Save::SaveFileRedirect
