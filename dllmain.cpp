#include "stdafx.h"
#include "hooks.h"
#include "logger.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*lpReserved*/)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        Logger& l = Logger::Instance();
        l.InitializeConsole(); // Add this line

        l.Get()->info("DLL_PROCESS_ATTACH");
        l.Get()->flush();

        char currentDir[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, currentDir);
        l.Get()->info("Current working directory: {}", currentDir);

        uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));
        IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        IMAGE_NT_HEADERS* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);

        if ((base + nt->OptionalHeader.AddressOfEntryPoint + (0x400000 - base)) == 0x4B88FD ||
            (base + nt->OptionalHeader.AddressOfEntryPoint + (0x400000 - base)) == 0x730310)
        {
            l.Get()->info("Correct EXE version detected. Base address: 0x{:x}", base);

            HANDLE hThread = CreateThread(NULL, 0, InitThread, reinterpret_cast<LPVOID>(base), 0, NULL);
            if (hThread)
            {
                CloseHandle(hThread);
            }
            else
            {
                l.Get()->error("Failed to create initialization thread");
            }
        }
        else
        {
            MessageBoxA(NULL, "This .exe is not supported.\nPlease use the Steam version", "Hook Crashers", MB_ICONERROR);
            return FALSE;
        }
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        Logger::Instance().Shutdown();
    }
    return TRUE;
}