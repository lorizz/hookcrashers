#pragma once

#include <cstdint>

extern "C" __declspec(dllexport)
bool __stdcall HookCrashers_RegisterCharacter(const char* name,
                                              std::uint8_t startingWeapon,
                                              std::uint8_t startingPet,
                                              bool initiallyUnlocked,
                                              bool freshOnly);

extern "C" __declspec(dllexport)
bool __stdcall HookCrashers_InstallCharacterExpansion();

extern "C" __declspec(dllexport)
void __stdcall HookCrashers_RemoveCharacterExpansion();

extern "C" __declspec(dllexport)
bool __stdcall HookCrashers_GetInitSaveSystemBytecode(const std::uint8_t** data, std::uint32_t* size);
