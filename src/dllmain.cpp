#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "NativeDB/Natives.h"

namespace {

constexpr std::uintptr_t kSteam32TargetAddress = 0x0085E310;
constexpr std::uintptr_t kPePreferredImageBase = 0x00400000;

std::uintptr_t ReadTargetAddress(std::uintptr_t moduleBase) {
    if (moduleBase == 0) {
        return 0;
    }

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(moduleBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return 0;
    }

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(moduleBase + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return 0;
    }

    const std::uintptr_t entryPoint = moduleBase + nt->OptionalHeader.AddressOfEntryPoint;
    return entryPoint + (kPePreferredImageBase - moduleBase);
}

bool IsSupportedTarget(std::uintptr_t targetAddress) {
    return targetAddress == kSteam32TargetAddress;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);

        const auto gameBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
        const std::uintptr_t targetAddress = ReadTargetAddress(gameBase);
        if (!IsSupportedTarget(targetAddress)) {
            char message[256] {};
            std::snprintf(message,
                          sizeof(message),
                          "Unsupported Castle Crashers executable.\n"
                          "Expected Steam 3.2 targetAddress: 0x%08X\n"
                          "Current targetAddress: 0x%08X",
                          static_cast<unsigned int>(kSteam32TargetAddress),
                          static_cast<unsigned int>(targetAddress));
            MessageBoxA(nullptr, message, "HookCrashers", MB_ICONERROR | MB_OK);
            return FALSE;
        }

        HookCrashers::NativeDB::SetGameBase(gameBase);
    }

    return TRUE;
}
