#pragma once
#include <Windows.h>
#include <stdint.h>

// Function to setup the hook for FUN_00c002a0
void SetupRegisterAllSWFFunctionsHook(uintptr_t moduleBase);

// The hooked function declaration (internal use)
void __fastcall RegisterAllSWFFunctionsHook(int** param_1);