#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "Characters/CharacterExpansion.h"
#include "Characters/CharacterRegistry.h"
#include "Core/Config.h"
#include "Core/CrashHandler.h"
#include "Core/Logger.h"
#include "Core/ModLoader.h"
#include "NativeDB/Natives.h"
#include "Localization/LocalizationSystem.h"
#include "Save/SaveFileRedirect.h"
#include "UI/DebugOverlay.h"
#include "UI/SplashScreen.h"
#include "UI/UpdatePromptMenu.h"
#include "Updater/AutoUpdater.h"

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

DWORD WINAPI BootstrapThread(void* parameter) {
    auto module = static_cast<HMODULE>(parameter);
    HookCrashers::Core::Logger::Initialize(module);
    HookCrashers::Core::Logger::Info("Bootstrap thread started");


    HookCrashers::Core::CrashHandler::Install(HookCrashers::Core::Logger::ModuleDirectory());

    const auto gameBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
    const std::uintptr_t targetAddress = ReadTargetAddress(gameBase);
    HookCrashers::Core::Logger::Info("Game base=0x%08X targetAddress=0x%08X",
                                    static_cast<unsigned int>(gameBase),
                                    static_cast<unsigned int>(targetAddress));
    if (!IsSupportedTarget(targetAddress)) {
        HookCrashers::Core::Logger::Error("Unsupported target. Expected=0x%08X current=0x%08X",
                                         static_cast<unsigned int>(kSteam32TargetAddress),
                                         static_cast<unsigned int>(targetAddress));
        char message[256] {};
        std::snprintf(message,
                      sizeof(message),
                      "Unsupported Castle Crashers executable.\n"
                      "Expected Steam 3.2 targetAddress: 0x%08X\n"
                      "Current targetAddress: 0x%08X\n\n"
                      "See HookCrashers.log for details.",
                      static_cast<unsigned int>(kSteam32TargetAddress),
                      static_cast<unsigned int>(targetAddress));
        MessageBoxA(nullptr, message, "HookCrashers", MB_ICONERROR | MB_OK);
        return 1;
    }

    HookCrashers::NativeDB::SetGameBase(gameBase);
    HookCrashers::Core::Logger::Info("NativeDB game base set");

    HookCrashers::Localization::Reset();
    if (!HookCrashers::Localization::Install()) {
        HookCrashers::Core::Logger::Error("Localization system failed to install");
        return 1;
    }

    if (!HookCrashers::UI::UpdatePromptMenu::Install()) {
        HookCrashers::Core::Logger::Warn("Update prompt menu hook failed to install");
    }

    const HookCrashers::Core::Config::RuntimeConfig config =
        HookCrashers::Core::Config::LoadOrCreate(HookCrashers::Core::Logger::ModuleDirectory());
    HookCrashers::Core::Logger::SetExternalConsoleEnabled(config.showExternalConsole);

    const bool updateAvailable = HookCrashers::Updater::AutoUpdater::CheckForUpdates(
        HookCrashers::Core::Logger::ModuleDirectory(), config);
    HookCrashers::UI::UpdatePromptMenu::SetStartupUpdateAvailable(updateAvailable);

    const HookCrashers::Core::ModLoader::LoadResult loadResult =
        HookCrashers::Core::ModLoader::LoadAll(HookCrashers::Core::Logger::ModuleDirectory());
    HookCrashers::Core::Logger::Info("ModLoader result: mods=%d scripts=%d characters=%d registryAddons=%d",
                                    loadResult.modsFound,
                                    loadResult.scriptsRun,
                                    loadResult.charactersRegistered,
                                    HookCrashers::Characters::Registry::Instance().AddonCount());

    if (loadResult.modsFound > 0 &&
        !HookCrashers::Save::SaveFileRedirect::Install(loadResult.manifestName)) {
        HookCrashers::Core::Logger::Error("Save filename redirect failed; refusing to continue with modded saves");
        MessageBoxA(nullptr,
                    "HookCrashers could not isolate the modded save. The vanilla cc_save.dat was not touched.",
                    "HookCrashers",
                    MB_ICONERROR | MB_OK);
        return 1;
    }

    if (HookCrashers::Characters::Registry::Instance().AddonCount() > 0) {
        const bool installed = HookCrashers::Characters::CharacterExpansion::Install();
        HookCrashers::Core::Logger::Info("Character expansion auto-install result=%d", installed ? 1 : 0);
        if (!installed) {
            MessageBoxA(nullptr,
                        "HookCrashers failed to install character expansion. Check HookCrashers.log.",
                        "HookCrashers",
                        MB_ICONERROR | MB_OK);
            return 1;
        }
    } else {
        HookCrashers::Core::Logger::Warn("No addon characters registered; character expansion was not installed");
    }

    if (config.enableOverlay) {
        if (!HookCrashers::UI::DebugOverlay::Install(false, config.overlayToggleKey)) {
            HookCrashers::Core::Logger::Warn("Debug overlay failed to install; continuing without ImGui console");
        }
    } else {
        HookCrashers::Core::Logger::Info("Debug overlay disabled by HookCrashers.yaml");
    }

    HookCrashers::UI::SplashScreen::Show(module, 1000);

    HookCrashers::Core::Logger::Info("Bootstrap thread finished");
    return 0;
}
} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, BootstrapThread, module, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        } else {
            return FALSE;
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        HookCrashers::Core::Logger::Info("DllMain PROCESS_DETACH");
        HookCrashers::UI::DebugOverlay::Shutdown();
        HookCrashers::UI::UpdatePromptMenu::Remove();
        HookCrashers::Localization::Remove();
        HookCrashers::Core::ModLoader::Shutdown();
        HookCrashers::Characters::CharacterExpansion::Remove();
        HookCrashers::Core::CrashHandler::Uninstall();
        HookCrashers::Core::Logger::Shutdown();
    }

    return TRUE;
}
