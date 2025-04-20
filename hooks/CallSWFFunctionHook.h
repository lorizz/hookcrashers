#pragma once
#include <Windows.h>
#include <stdint.h>
#include "../structs/SWFArgument.h"

// Setup dell'hook per FUN_00bfa070
void SetupCallSWFFunctionHook(uintptr_t moduleBase);