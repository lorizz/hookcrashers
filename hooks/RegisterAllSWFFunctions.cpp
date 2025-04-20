#include "../stdafx.h"
#include "RegisterAllSWFFunctions.h"
#include "RegisterSWFFunctionHook.h"
#include <Windows.h>
#include <detours.h>
#include "../logger.h"
#include "CustomSWFFunctions.h"

// Original function pointer
typedef void(__fastcall* RegisterAllSWFFunctions_t)(int** param_1);
RegisterAllSWFFunctions_t originalFunction = nullptr;

// Hooked function implementation
void __fastcall RegisterAllSWFFunctionsHook(int** param_1)
{
    Logger& l = Logger::Instance();

    // Call the original function to handle all standard registrations
    if (originalFunction) {
        originalFunction(param_1);
    }

    // After original function is complete, add our custom functions
    l.Get()->info("Adding custom SWF functions to the game registry");

    // Get the correct thisPtr that we captured
    void* correctThisPtr = GetCorrectThisPtr();
    if (correctThisPtr != nullptr) {
        // Get all registered custom function handlers
        auto registeredIds = CustomSWFFunctions::GetRegisteredFunctionIds();

        if (registeredIds.empty()) {
            l.Get()->warn("No custom function handlers registered yet");
        }
        else {
            l.Get()->info("Found {} custom function handlers to register with the game", registeredIds.size());

            // For each function handler we've registered, tell the game about it
            for (uint16_t id : registeredIds) {
                // For HelloWorld specifically
                if (id == ToValue(SWFFunctionID::HelloWorld)) {
                    l.Get()->info("Registering HelloWorld function with the game, ID: {}", id);
                    RegisterSWFFunction(correctThisPtr, nullptr, id, "HelloWorld");
                }
                // You can add cases for other custom functions here
                // else if (id == ToValue(SWFFunctionID::AnotherFunction)) { ... }
            }
        }

        // For backward compatibility, ensure HelloWorld is registered even if not in the map
        uint16_t helloWorldId = ToValue(SWFFunctionID::HelloWorld);
        if (std::find(registeredIds.begin(), registeredIds.end(), helloWorldId) == registeredIds.end()) {
            l.Get()->info("HelloWorld handler wasn't registered yet, registering with the game anyway, ID: {}", helloWorldId);
            RegisterSWFFunction(correctThisPtr, nullptr, helloWorldId, "HelloWorld");
        }
    }
    else {
        l.Get()->error("Could not register custom functions with the game, correctThisPtr is NULL");
    }
    l.Get()->flush();
}

void SetupRegisterAllSWFFunctionsHook(uintptr_t moduleBase)
{
    Logger& l = Logger::Instance();

    // Address of the original function to hook (FUN_00c002a0)
    uintptr_t targetAddress = moduleBase + 0x802A0;

    // Save the original function address
    originalFunction = reinterpret_cast<RegisterAllSWFFunctions_t>(targetAddress);

    // Begin the detour transaction
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    // Attach the hook
    LONG error = DetourAttach(&(PVOID&)originalFunction, RegisterAllSWFFunctionsHook);
    if (error != NO_ERROR) {
        l.Get()->error("Error on RegisterAllSWFFunctions hook: {}", error);
        l.Get()->flush();
        DetourTransactionAbort();
        return;
    }

    // Commit the transaction
    error = DetourTransactionCommit();
    if (error != NO_ERROR) {
        l.Get()->error("Error on RegisterAllSWFFunctions commit: {}", error);
        l.Get()->flush();
        return;
    }

    l.Get()->info("RegisterAllSWFFunctions successfully hooked! (0x{:x})", targetAddress);
    l.Get()->flush();
}