#include "stdafx.h"
#include "Util/Logger.h"         // Use namespaced logger
#include "Core/HookManager.h"    // Use HookManager for initialization
#include "../include/HookCrashersAPI.h" // Include public API

// Global pointer to the game's module handle/base address
// Useful if needed outside the initialization thread
uintptr_t g_ModuleBase = 0;

// Initialization thread function
DWORD WINAPI InitThread(LPVOID lpParam) {
    uintptr_t base = reinterpret_cast<uintptr_t>(lpParam);
    g_ModuleBase = base; // Store globally if needed

    // Initialize the console logger first
    HookCrashers::Util::Logger::Instance().InitializeConsole();
    HookCrashers::Util::Logger::Instance().Get()->info("Initialization thread started. Module Base: 0x{:X}", base);

    // Initialize all hooks and subsystems via the HookManager
    if (!HookCrashers::Core::HookManager::Initialize(base)) {
        HookCrashers::Util::Logger::Instance().Get()->critical("!!! HOOK INITIALIZATION FAILED !!!");
        // Optionally show a message box or take other action
        MessageBoxA(NULL, "Hook Crashers failed to initialize critical components.\nThe mod may not function correctly.", "Hook Init Error", MB_ICONERROR | MB_OK);
    }

    HookCrashers::Util::Logger::Instance().Get()->info("Initialization thread finished.");
    HookCrashers::Util::Logger::Instance().Get()->flush();

    return 0; // Thread finished
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*lpReserved*/) {
    DisableThreadLibraryCalls(hModule); // Optimization for DLLs that don't need thread attach/detach notifications

    if (reason == DLL_PROCESS_ATTACH) {
        // Basic logging setup immediately (file log)
        // Console log is set up in InitThread after console is allocated.
        auto& loggerInstance = HookCrashers::Util::Logger::Instance(); // Ensure instance creation
        loggerInstance.Get()->info("===== HookCrashers DLL Attached (PID: {}) =====", GetCurrentProcessId());
        loggerInstance.Get()->flush();


        // Get game base address more reliably
        uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL)); // Get base of host process
        if (base == 0) {
            loggerInstance.Get()->critical("GetModuleHandleA(NULL) failed! Cannot get game base address.");
            MessageBoxA(NULL, "Failed to get game module handle.", "Hook Crashers Error", MB_ICONERROR);
            return FALSE; // Prevent injection if base address fails
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
        // Perform cleanup
        HookCrashers::Util::Logger::Instance().Get()->info("===== HookCrashers DLL Detaching =====");
        HookCrashers::Core::HookManager::Shutdown(); // Detach hooks if implemented
        HookCrashers::Util::Logger::Instance().Shutdown(); // Shutdown logger last
    }
    return TRUE; // Successful attach/detach
}