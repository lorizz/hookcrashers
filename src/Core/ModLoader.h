#pragma once

#include <string>
#include <vector>

namespace HookCrashers::Core::ModLoader {

struct LoadResult {
    int modsFound = 0;
    int nativeModulesLoaded = 0;
    int scriptsRun = 0;
    int charactersRegistered = 0;
    std::string manifestName;
};

struct LoadedMod {
    std::string folder;
    std::string name;     // manifest name, or the folder name without a manifest
    std::string version;  // manifest version, may be empty
    int characters = 0;   // addon characters it registered (these change the save layout)
};

LoadResult LoadAll(const std::string& rootDirectory);
void Shutdown(bool unloadNativeModules = true);
const std::string& CurrentNativeModDirectory();
// Mods found by the last LoadAll, in load order.
const std::vector<LoadedMod>& LoadedMods();

} // namespace HookCrashers::Core::ModLoader
