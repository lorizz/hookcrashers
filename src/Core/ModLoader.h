#pragma once

#include <string>

namespace HookCrashers::Core::ModLoader {

struct LoadResult {
    int modsFound = 0;
    int scriptsRun = 0;
    int charactersRegistered = 0;
};

LoadResult LoadAll(const std::string& rootDirectory);

} // namespace HookCrashers::Core::ModLoader
