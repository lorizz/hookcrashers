#include <HookCrashers.h>

HOOKCRASHERS_MOD_EXPORT bool __stdcall HookCrashersModInit() {
    HookCrashers::LogInfo("ExampleNativeMod initialized");
    return true;
}

HOOKCRASHERS_MOD_EXPORT void __stdcall HookCrashersModShutdown() {
    HookCrashers::LogInfo("ExampleNativeMod shutting down");
}
