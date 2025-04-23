#include "stdafx.h"
#include "Util/Logger.h"
#include "Core/HookManager.h"
#include "../include/HookCrashersAPI.h"

uintptr_t g_ModuleBase = 0;

DWORD WINAPI InitThread(LPVOID lpParam) {
    uintptr_t base = reinterpret_cast<uintptr_t>(lpParam);
    g_ModuleBase = base;

    HookCrashers::Util::Logger::Instance().InitializeConsole();
    HookCrashers::Util::Logger::Instance().Get()->info("Initialization thread started. Module Base: 0x{:X}", base);

    if (!HookCrashers::Core::HookManager::Initialize(base)) {
        HookCrashers::Util::Logger::Instance().Get()->critical("!!! HOOK INITIALIZATION FAILED !!!");
        MessageBoxA(NULL, "Hook Crashers failed to initialize critical components.\nThe mod may not function correctly.", "Hook Init Error", MB_ICONERROR | MB_OK);
    }

    HookCrashers::Util::Logger::Instance().Get()->info("Initialization thread finished.");
    HookCrashers::Util::Logger::Instance().Get()->flush();

    return 0;
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*lpReserved*/) {
    DisableThreadLibraryCalls(hModule);

    if (reason == DLL_PROCESS_ATTACH) {
        auto& loggerInstance = HookCrashers::Util::Logger::Instance();
        loggerInstance.Get()->info("===== HookCrashers DLL Attached (PID: {}) =====", GetCurrentProcessId());
        loggerInstance.Get()->flush();

        uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
        if (base == 0) {
            loggerInstance.Get()->critical("GetModuleHandleA(NULL) failed! Cannot get game base address.");
            MessageBoxA(NULL, "Failed to get game module handle.", "Hook Crashers Error", MB_ICONERROR);
            return FALSE;
        }
        loggerInstance.Get()->info("Detected Game Base Address: 0x{:X}", base);

        IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        IMAGE_NT_HEADERS* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        uintptr_t entryPointAddr = base + nt->OptionalHeader.AddressOfEntryPoint;
        loggerInstance.Get()->info("Game Entry Point Address: 0x{:X}", entryPointAddr);
        
        uintptr_t versionCheckValue = entryPointAddr + (0x400000 - base);
        loggerInstance.Get()->info("Version Check Value (Entry + (Preferred Base - Actual Base)): 0x{:X}", versionCheckValue);

        if (versionCheckValue == 0x4B88FD || versionCheckValue == 0x730310)
        {
            loggerInstance.Get()->info("Supported EXE version detected.");
            HANDLE hThread = CreateThread(NULL, 0, InitThread, reinterpret_cast<LPVOID>(base), 0, NULL);
            if (hThread) {
                loggerInstance.Get()->info("Initialization thread created successfully.");
                CloseHandle(hThread);
            }
            else {
                loggerInstance.Get()->error("Failed to create initialization thread! Error code: {}", GetLastError());
                MessageBoxA(NULL, "Failed to create initialization thread.", "Hook Crashers", MB_ICONERROR);
                return FALSE;
            }
        }
        else {
            loggerInstance.Get()->error("Unsupported EXE version detected! Version Check Value: 0x{:X}", versionCheckValue);
            MessageBoxA(NULL, "This .exe version is not supported by Hook Crashers.\nPlease use a compatible Steam version or debug.", "Hook Crashers", MB_ICONERROR);
            return FALSE;
        }
    }
    else if (reason == DLL_PROCESS_DETACH) {
        HookCrashers::Util::Logger::Instance().Get()->info("===== HookCrashers DLL Detaching =====");
        HookCrashers::Util::Logger::Instance().Shutdown();
    }
    return TRUE;
}