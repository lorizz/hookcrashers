#include "NativeApi.h"

#include "../Core/Logger.h"
#include "../Core/Version.h"

extern "C" __declspec(dllexport)
const char* __stdcall HookCrashers_GetVersionString() {
    return HookCrashers::Core::Version::kVersion;
}

extern "C" __declspec(dllexport)
void __stdcall HookCrashers_LogInfo(const char* message) {
    HookCrashers::Core::Logger::Info("Native mod: %s", message ? message : "");
}

extern "C" __declspec(dllexport)
void __stdcall HookCrashers_LogWarn(const char* message) {
    HookCrashers::Core::Logger::Warn("Native mod: %s", message ? message : "");
}

extern "C" __declspec(dllexport)
void __stdcall HookCrashers_LogError(const char* message) {
    HookCrashers::Core::Logger::Error("Native mod: %s", message ? message : "");
}
