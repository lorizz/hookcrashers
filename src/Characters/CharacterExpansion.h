#pragma once

namespace HookCrashers::Characters {

// Default data of addon `addonIndex` (0-based, registration order) written to a
// 0x30-byte save character record. Used when a save gains a character.
void InitializeAddonCharacter(void* character, int addonIndex);

class CharacterExpansion {
public:
    static bool Install();
    static void Remove();
};

} // namespace HookCrashers::Characters
