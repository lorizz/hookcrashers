#pragma once

#include <string>

namespace HookCrashers {
namespace Config {

struct Settings {
    bool showExternalConsole = false;
    bool enableOverlay = true;
    int overlayToggleVirtualKey = 0x24; // VK_HOME
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
