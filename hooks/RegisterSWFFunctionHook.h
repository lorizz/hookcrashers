#pragma once
#include <Windows.h>
#include <stdint.h>

// Add this export declaration
extern "C" __declspec(dllexport) void __fastcall RegisterSWFFunction(void* thisPtr, void* edx, WORD functionId, const char* functionName);

void* GetCorrectThisPtr();
int GetLastFunctionId();
int GetAvailableFunctionId();

void SetupRegisterSWFFunctionHook(uintptr_t moduleBase);