#pragma once

namespace HookCrashers::SWF {

class Cok6LoadHook {
public:
    // CharacterExpansion uses the default, including explicit Lua/C++ installs.
    // Menu startup only needs the loader and must pass false.
    static bool Install(bool enableCharacterPatches = true);
    static void Remove();
};

} // namespace HookCrashers::SWF
