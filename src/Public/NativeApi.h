#pragma once

extern "C" __declspec(dllexport)
const char* __stdcall HookCrashers_GetVersionString();

extern "C" __declspec(dllexport)
void __stdcall HookCrashers_LogInfo(const char* message);

extern "C" __declspec(dllexport)
void __stdcall HookCrashers_LogWarn(const char* message);

extern "C" __declspec(dllexport)
void __stdcall HookCrashers_LogError(const char* message);
