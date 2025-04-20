#include "../stdafx.h"
#include "CallSWFFunctionHook.h"
#include "RegisterSWFFunctionHook.h"
#include "HookCrashers.h"
#include <Windows.h>
#include <detours.h>
#include "../logger.h"
#include "CustomSWFFunctions.h"

typedef void(__thiscall* OriginalFunc_t)(void* thisPtr, int param_1, uint32_t param_2,
    int param_3, SWFArgument** swfArgs, uint32_t* param_5,
    uint32_t param_6);

OriginalFunc_t originalFunction = nullptr;

void __fastcall CallSWFFunctionHook(void* thisPtr, void* edx, int param_1, uint32_t param_2,
    int param_3, SWFArgument** swfArgs, uint32_t* param_5,
    uint32_t param_6) {

    Logger& l = Logger::Instance();
    uint16_t functionId = param_2 & 0xFFFF;

    if (functionId > 5000) {
        l.Get()->info("CallSWFFunction called with ID: 0x{:X}", functionId);
    }

    if (CustomSWFFunctions::IsCustomFunction(functionId)) {
        l.Get()->info("Custom SWF Function identified with ID 0x{:X}", functionId);
        l.Get()->flush();

        SWFReturn* returnValue = SWFReturnHelper::AsStructured(param_5);
        if (CustomSWFFunctions::HandleCustomCall(functionId, thisPtr, param_1,
            param_3, swfArgs, returnValue, param_6)) {
            l.Get()->info("Custom function handled successfully");
            l.Get()->flush();
            return;
        }
        l.Get()->warn("Custom function was not handled");
    }

    auto hookFunc = HookCrashers::GetHookFunction(functionId);
    if (hookFunc) {
        l.Get()->info("Executing override for function ID 0x{:X}", functionId);
        l.Get()->flush();
        // Convert parameters to match the hook function signature
        SWFReturn* returnValue = SWFReturnHelper::AsStructured(param_5);
        hookFunc(thisPtr, param_1, param_2, param_3, swfArgs, returnValue, param_6);
        return; // Override executed, don't call original function
    }
    else {
        // Fall back to original function
        if (originalFunction) {
            originalFunction(thisPtr, param_1, param_2, param_3, swfArgs, param_5, param_6);
        }
    }
}

void SetupCallSWFFunctionHook(uintptr_t moduleBase) {
    Logger& l = Logger::Instance();
    uintptr_t targetAddress = moduleBase + 0x7A070;
    originalFunction = reinterpret_cast<OriginalFunc_t>(targetAddress);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG error = DetourAttach(&(PVOID&)originalFunction, CallSWFFunctionHook);
    if (error != NO_ERROR) {
        l.Get()->error("Error hooking CallSWFFunction: {}", error);
        DetourTransactionAbort();
        return;
    }
    error = DetourTransactionCommit();
    if (error != NO_ERROR) {
        l.Get()->error("Error committing hook: {}", error);
    }
}