#include "../stdafx.h"
#include "RegisterSWFFunctionHook.h"
#include <Windows.h>
#include <detours.h>
#include "../logger.h"

typedef void(__thiscall* OriginalFunc_t)(void* thisPtr, WORD param1, const char* param2);
OriginalFunc_t originalFunction = nullptr;

void* g_correctThisPtr = nullptr;
int g_lastFunctionId = -1;

void __fastcall RegisterSWFFunction(void* thisPtr, void* /*edxUnused*/, WORD functionId, const char* functionName) {
    Logger& l = Logger::Instance();

    if (functionId == 0x13A && strcmp(functionName, "u_temp") == 0) {
        g_correctThisPtr = thisPtr;
        g_lastFunctionId = 0x13A;
    }

    // Original function call
    if (originalFunction) {
        originalFunction(thisPtr, functionId, functionName);
    }
}

void* GetCorrectThisPtr() {
    return g_correctThisPtr;
}

int GetLastFunctionId() {
    return g_lastFunctionId;
}

int GetAvailableFunctionId() {
    return ++g_lastFunctionId;
}

void SetupRegisterSWFFunctionHook(uintptr_t moduleBase) {
    Logger& l = Logger::Instance();
    uintptr_t targetAddress = moduleBase + 0x5D370;

    // Salva l'indirizzo della funzione originale
    originalFunction = reinterpret_cast<OriginalFunc_t>(targetAddress);

    // Inizia la transazione per l'hooking
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    // Aggiungi l'hook
    LONG error = DetourAttach(&(PVOID&)originalFunction, RegisterSWFFunction);
    if (error != NO_ERROR) {
        l.Get()->error("Error on RegisterSWFFunction hook: {}", error);
        l.Get()->flush();
        DetourTransactionAbort();
        return;
    }

    // Conferma la transazione
    error = DetourTransactionCommit();
    if (error != NO_ERROR) {
        l.Get()->error("Error on RegisterSWFFunction commit: {}", error);
        l.Get()->flush();
        return;
    }

    l.Get()->info("RegisterSWFFunction successfully hooked! (0x{:x})", targetAddress);
    l.Get()->flush();
}