#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace HookCrashers {
namespace Config {

struct AddonCharacterDef {
    std::string id;
    uint8_t weapon = 0;
    uint8_t animal = 0;
    bool unlocked = false;
    bool freshOnly = false;
    std::string portraitClassicPath;
    std::string portraitFreshPath;
};

struct Settings {
    bool showExternalConsole = false;
    bool enableOverlay = true;
    int overlayToggleVirtualKey = 0x24; // VK_HOME
    bool enableCustomLocalizations = true;
    int localizationBaseId = 5000;
    std::string localizationPath = "HookCrashers\\Localizations\\";
    std::vector<AddonCharacterDef> addonCharacters;
};

class HookCrashersConfig {
public:
    static HookCrashersConfig& Instance();

    bool Load(const std::string& path);
    bool SaveDefault(const std::string& path) const;

    const Settings& Get() const { return m_settings; }
    const std::string& GetPath() const { return m_path; }

private:
    HookCrashersConfig() = default;

    Settings m_settings;
    std::string m_path;
};

} // namespace Config
} // namespace HookCrashers
