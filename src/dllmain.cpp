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

        uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
        if (base == 0) {
            MessageBoxA(NULL, "Failed to get game module handle.", "Hook Crashers Error", MB_ICONERROR);
            return FALSE;
        }

        IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        IMAGE_NT_HEADERS* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        uintptr_t entryPointAddr = base + nt->OptionalHeader.AddressOfEntryPoint;
        
        uintptr_t versionCheckValue = entryPointAddr + (0x400000 - base);

        //if (versionCheckValue == 0x4B88FD || versionCheckValue == 0x730310) // Prior to 3.0
        if (versionCheckValue == 0x85B310 || versionCheckValue == 0x56375F) // 3.0
        {
            HANDLE hThread = CreateThread(NULL, 0, InitThread, reinterpret_cast<LPVOID>(base), 0, NULL);
            if (hThread) {
                CloseHandle(hThread);
            }
            else {
                MessageBoxA(NULL, "Failed to create initialization thread.", "Hook Crashers", MB_ICONERROR);
                return FALSE;
            }
        }
        else {
            char msg[256];
            sprintf_s(msg, sizeof(msg),
                "This .exe version is not supported by Hook Crashers.\n"
                "Please use a compatible Steam version or debug.\n"
                "BaseAddress: 0x%p", (void*)versionCheckValue);

            MessageBoxA(NULL, msg, "Hook Crashers", MB_ICONERROR);
            return FALSE;
        }
    }
    else if (reason == DLL_PROCESS_DETACH) {
        HookCrashers::Util::Logger::Instance().Get()->info("===== HookCrashers DLL Detaching =====");
        HookCrashers::Util::Logger::Instance().Shutdown();
    }
    return TRUE;
}

namespace HookCrashers {
    spdlog::logger* GetLogger() {
        return Util::Logger::Instance().Get();
    }
}