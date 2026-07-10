#include "CharacterApi.h"

#include "CharacterExpansion.h"
#include "CharacterRegistry.h"
#include "../SWF/InitSaveSystemBytecode.h"

#include <vector>

extern "C" __declspec(dllexport)
bool __stdcall HookCrashers_RegisterCharacter(const char* name,
                                              std::uint8_t startingWeapon,
                                              std::uint8_t startingPet,
                                              bool initiallyUnlocked,
                                              bool freshOnly) {
    HookCrashers::Characters::AddonCharacter character;
    character.name = name ? name : "";
    character.startingWeapon = startingWeapon;
    character.startingPet = startingPet;
    character.initiallyUnlocked = initiallyUnlocked;
    character.freshOnly = freshOnly;
    return HookCrashers::Characters::Registry::Instance().RegisterCharacter(character);
}

extern "C" __declspec(dllexport)
bool __stdcall HookCrashers_InstallCharacterExpansion() {
    return HookCrashers::Characters::CharacterExpansion::Install();
}

extern "C" __declspec(dllexport)
void __stdcall HookCrashers_RemoveCharacterExpansion() {
    HookCrashers::Characters::CharacterExpansion::Remove();
}

extern "C" __declspec(dllexport)
bool __stdcall HookCrashers_GetInitSaveSystemBytecode(const std::uint8_t** data, std::uint32_t* size) {
    static std::vector<std::uint8_t> bytecode;
    bytecode = HookCrashers::SWF::BuildInitSaveSystemBytecode();
    if (!data || !size || bytecode.empty()) {
        return false;
    }
    *data = bytecode.data();
    *size = static_cast<std::uint32_t>(bytecode.size());
    return true;
}
