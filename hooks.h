#pragma once
#include <Windows.h>
#include <stdint.h>

// Dichiarazioni di funzioni
//void __fastcall RegisterSWFFunctions(void* thisPtr, void* /*edxUnused*/, WORD functionId, const char* functionName);
//void SetupHook(uintptr_t moduleBase);
void InitializeAllHooks(uintptr_t moduleBase);

DWORD WINAPI InitThread(LPVOID lpParam);