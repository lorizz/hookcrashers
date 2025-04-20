#include "stdafx.h"
#include "hooks.h"
#include "hooks/RegisterSWFFunctionHook.h"
#include "hooks/CallSWFFunctionHook.h"
#include "hooks/RegisterAllSWFFunctions.h"
#include <Windows.h>
#include "logger.h"
#include "hooks/HookCrashers.h"
#include "hooks/NativeHooks.h"
#include "structs/SWFArgument.h"
#include "structs/SWFReturn.h"
#include "structs/SWFFunctionIDs.h"
#include "hooks/CustomSWFFunctions.h"

void InitializeOverrides(uintptr_t moduleBase) {
    // Override per ReadStorage (0x3F)
    HookCrashers::Override(SWFFunctionID::ReadStorage, [moduleBase](void* thisPtr, int swfContext, uint32_t functionId,
        int paramCount, SWFArgument** swfArgs, SWFReturn* swfReturn, uint32_t callbackPtr) {
            Logger& l = Logger::Instance();
            for (int i = 0; i < paramCount && i < 10; ++i) {
                SWFArgument* arg = swfArgs[i];
                if (arg) {
                }
                else {
                }
            }

            if (paramCount > 0) {
                int storageIndex = SWFArgumentReader::GetInteger(swfArgs[0]);

                if (paramCount > 1) {
                    int offset = SWFArgumentReader::GetInteger(swfArgs[1]);
                    if (offset == 544) {
                        swfReturn->SetIntegerSuccess(0x80);
                    }
                }
            }
        });
}

void InitializeAllHooks(uintptr_t moduleBase) {
    Logger& l = Logger::Instance();
    l.Get()->info("Initializing all hooks at base address: {:#x}", moduleBase);

    // Step 1: Set up RegisterSWFFunction hook first to capture the correct thisPtr
    SetupRegisterSWFFunctionHook(moduleBase);
    l.Get()->info("RegisterSWFFunction hook set up");

    // Step 2: Set up CallSWFFunction hook so we can intercept function calls
    SetupCallSWFFunctionHook(moduleBase);
    l.Get()->info("CallSWFFunction hook set up");

    // Step 3: Initialize CustomSWFFunctions system to register all function handlers
    CustomSWFFunctions::Initialize();
    l.Get()->info("Custom SWF functions initialized");

    // Step 4: Set up HookCrashers for general hooks and overrides
    HookCrashers::Initialize(moduleBase);
    l.Get()->info("HookCrashers system initialized");

    // Step 5: Register any additional overrides
    InitializeOverrides(moduleBase);
    l.Get()->info("Function overrides registered");

    // Step 6: Finally, set up RegisterAllSWFFunctions hook which will use the captured thisPtr
    // to register our custom functions with the game
    SetupRegisterAllSWFFunctionsHook(moduleBase);
    l.Get()->info("RegisterAllSWFFunctions hook set up");

    l.Get()->info("All hooks initialized successfully");
    l.Get()->flush();
}


DWORD WINAPI InitThread(LPVOID lpParam) {
    uintptr_t base = reinterpret_cast<uintptr_t>(lpParam);
    
    InitializeAllHooks(base);
    return 0;
}