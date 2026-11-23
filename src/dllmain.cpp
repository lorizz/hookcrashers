#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "Characters/CharacterExpansion.h"
#include "Characters/CharacterRegistry.h"
#include "Characters/CurrentCharacterData.h"
#include "Core/Config.h"
#include "Core/CrashHandler.h"
#include "Core/GamePoolExpansion.h"
#include "Core/GraphicsQuality.h"
#include "Core/Logger.h"
#include "Core/ModLoader.h"
#include "Core/Version.h"
#include "NativeDB/Natives.h"
#include "Localization/LocalizationSystem.h"
#include "Save/SaveFileRedirect.h"
#include "SWF/SWFStringTableExpansion.h"
#include "SWF/Cok6LoadHook.h"
#include "Save/MenuOnlineCompatibilityHook.h"
#include "UI/DebugOverlay.h"
#include "UI/SplashScreen.h"
#include "UI/UpdatePromptMenu.h"
#include "Updater/AutoUpdater.h"

namespace {

constexpr std::uintptr_t kSteam32TargetAddress = 0x0085E310;
constexpr std::uintptr_t kPePreferredImageBase = 0x00400000;
// The SteamStub entry point above is identical across Steam builds, so it cannot
// tell game updates apart. Every RVA in HookCrashers targets this exact build
// (castle.exe linked 2026-07-07 18:17:35 UTC); refuse anything else before patching.
constexpr DWORD kSupportedTimeDateStamp = 0x6A4D42BF;

DWORD ReadTimeDateStamp(std::uintptr_t moduleBase) {
    if (moduleBase == 0) {
        return 0;
    }
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(moduleBase);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return 0;
    }
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(moduleBase + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt->FileHeader.TimeDateStamp : 0;
}

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
    if (HookCrashers::Core::GraphicsQuality::IsInstalled()) {
        HookCrashers::Core::Logger::Info("Graphics presets: Low=0 Medium=1 High=2 Ultra=3; historical High enabled");
    } else {
        HookCrashers::Core::Logger::Error("Four graphics presets disabled: %s", HookCrashers::Core::GraphicsQuality::FailureReason());
    }


    HookCrashers::Core::CrashHandler::Install(HookCrashers::Core::Logger::ModuleDirectory());

    const auto gameBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
    const std::uintptr_t targetAddress = ReadTargetAddress(gameBase);
    HookCrashers::Core::Logger::Info("Game base=0x%08X targetAddress=0x%08X",
                                    static_cast<unsigned int>(gameBase),
                                    static_cast<unsigned int>(targetAddress));
    const DWORD timeDateStamp = ReadTimeDateStamp(gameBase);
    HookCrashers::Core::Logger::Info("Game TimeDateStamp=0x%08X supported=0x%08X",
                                    static_cast<unsigned int>(timeDateStamp),
                                    static_cast<unsigned int>(kSupportedTimeDateStamp));
    if (timeDateStamp != kSupportedTimeDateStamp) {
        HookCrashers::Core::Logger::Error("Unsupported game build. Expected TimeDateStamp=0x%08X current=0x%08X; no patches applied",
                                         static_cast<unsigned int>(kSupportedTimeDateStamp),
                                         static_cast<unsigned int>(timeDateStamp));
        char message[320] {};
        std::snprintf(message,
                      sizeof(message),
                      "This Castle Crashers build is not supported by HookCrashers %s.\n"
                      "Expected build: 0x%08X\n"
                      "Current build: 0x%08X\n\n"
                      "The game was probably updated by Steam. HookCrashers made no changes;\n"
                      "check for a HookCrashers update. See HookCrashers.log for details.",
                      HookCrashers::Core::Version::kVersion,
                      static_cast<unsigned int>(kSupportedTimeDateStamp),
                      static_cast<unsigned int>(timeDateStamp));
        MessageBoxA(nullptr, message, "HookCrashers", MB_ICONERROR | MB_OK);
        return 1;
    }

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

    if (!HookCrashers::SWF::SWFStringTableExpansion::Install()) {
        HookCrashers::Core::Logger::Error("SWF string table expansion failed to install");
        return 1;
    }

    HookCrashers::Localization::Reset();
    if (!HookCrashers::Localization::Install()) {
        HookCrashers::Core::Logger::Error("Localization system failed to install");
        return 1;
    }

    if (!HookCrashers::UI::UpdatePromptMenu::Install()) {
        HookCrashers::Core::Logger::Warn("Update prompt menu hook failed to install");
    }
    if (!HookCrashers::SWF::Cok6LoadHook::Install(false) || !HookCrashers::Save::SetupMenuOnlineCompatibilityHook()) {
        HookCrashers::Core::Logger::Error("Online compatibility menus could not be installed");
    }

    const HookCrashers::Core::Config::RuntimeConfig config =
        HookCrashers::Core::Config::LoadOrCreate(HookCrashers::Core::Logger::ModuleDirectory());
    HookCrashers::Core::Logger::SetExternalConsoleEnabled(config.showExternalConsole);
    if (!HookCrashers::Core::GamePoolExpansion::Install(config.gamePoolMiB)) {
        HookCrashers::Core::Logger::Error("Game pool expansion could not be enabled; see allocator compatibility log");
    }

    if (!HookCrashers::Characters::CurrentData::Install()) {
        HookCrashers::Core::Logger::Warn("Current character data API unavailable: capture hook not installed");
    }

    const bool updateAvailable = HookCrashers::Updater::AutoUpdater::CheckForUpdates(
        HookCrashers::Core::Logger::ModuleDirectory(), config);
    HookCrashers::UI::UpdatePromptMenu::SetStartupUpdateAvailable(updateAvailable);

    const HookCrashers::Core::ModLoader::LoadResult loadResult =
        HookCrashers::Core::ModLoader::LoadAll(HookCrashers::Core::Logger::ModuleDirectory());
    HookCrashers::Core::Logger::Info("ModLoader result: mods=%d nativeModules=%d scripts=%d characters=%d registryAddons=%d",
                                    loadResult.modsFound,
                                    loadResult.nativeModulesLoaded,
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

        const auto gameBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
        // Unsupported builds skip the early patches; BootstrapThread reports them.
        if (IsSupportedTarget(ReadTargetAddress(gameBase)) &&
            ReadTimeDateStamp(gameBase) == kSupportedTimeDateStamp) {
            HookCrashers::NativeDB::SetGameBase(gameBase);
            if (!HookCrashers::SWF::SWFStringTableExpansion::Install()) {
                return FALSE;
            }
            HookCrashers::Core::GraphicsQuality::Install();
        }

        HANDLE thread = CreateThread(nullptr, 0, BootstrapThread, module, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        } else {
            return FALSE;
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        HookCrashers::Core::Logger::Info("DllMain PROCESS_DETACH");
        HookCrashers::Core::GraphicsQuality::Shutdown();
        HookCrashers::Characters::CurrentData::Shutdown();
        HookCrashers::UI::DebugOverlay::Shutdown();
        HookCrashers::Save::RemoveMenuOnlineCompatibilityHook();
        HookCrashers::UI::UpdatePromptMenu::Remove();
        HookCrashers::Localization::Remove();
        HookCrashers::Core::ModLoader::Shutdown(false);
        HookCrashers::Characters::CharacterExpansion::Remove();
        HookCrashers::Core::GamePoolExpansion::Shutdown();
        HookCrashers::Core::CrashHandler::Uninstall();
        HookCrashers::Core::Logger::Shutdown();
    }

    return TRUE;
}
