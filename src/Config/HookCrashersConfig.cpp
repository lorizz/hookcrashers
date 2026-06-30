#include "HookCrashersConfig.h"
#include "../Util/Logger.h"
#include <Windows.h>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace {

std::string Trim(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), value.end());
    return value;
}

bool ParseBool(const std::string& value, bool defaultValue) {
    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") return true;
    if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off") return false;
    return defaultValue;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

int ParseVirtualKey(const std::string& value, int defaultValue) {
    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });

    if (normalized == "HOME") return VK_HOME;
    if (normalized == "INSERT") return VK_INSERT;
    if (normalized == "DELETE") return VK_DELETE;
    if (normalized == "F1") return VK_F1;
    if (normalized == "F2") return VK_F2;
    if (normalized == "F3") return VK_F3;
    if (normalized == "F4") return VK_F4;
    if (normalized == "F5") return VK_F5;
    if (normalized == "F6") return VK_F6;
    if (normalized == "F7") return VK_F7;
    if (normalized == "F8") return VK_F8;
    if (normalized == "F9") return VK_F9;
    if (normalized == "F10") return VK_F10;
    if (normalized == "F11") return VK_F11;
    if (normalized == "F12") return VK_F12;

    try {
        return std::stoi(value, nullptr, 0);
    }
    catch (...) {
        return defaultValue;
    }
}

} // namespace

namespace HookCrashers {
namespace Config {

HookCrashersConfig& HookCrashersConfig::Instance() {
    static HookCrashersConfig instance;
    return instance;
}

bool HookCrashersConfig::SaveDefault(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) return false;

    file
        << "[HookCrashers]\n"
        << "ShowExternalConsole=false\n"
        << "EnableOverlay=true\n"
        << "OverlayToggleKey=Home\n"
        << "\n";

    return true;
}

bool HookCrashersConfig::Load(const std::string& path) {
    m_path = path;
    m_settings = Settings{};

    std::ifstream file(path);
    if (!file.is_open()) {
        SaveDefault(path);
        HookCrashers::Util::Logger::Instance().Get()->warn("[Config] HookCrashers.ini not found. Created default config at '{}'.", path);
        return true;
    }

    std::string section;
    std::string line;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = ToLower(Trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = ToLower(Trim(line.substr(0, eq)));
        std::string value = Trim(line.substr(eq + 1));

        if (section == "hookcrashers") {
            if (key == "showexternalconsole") m_settings.showExternalConsole = ParseBool(value, m_settings.showExternalConsole);
            else if (key == "enableoverlay") m_settings.enableOverlay = ParseBool(value, m_settings.enableOverlay);
            else if (key == "overlaytogglekey") m_settings.overlayToggleVirtualKey = ParseVirtualKey(value, m_settings.overlayToggleVirtualKey);
        }
    }

    HookCrashers::Util::Logger::Instance().Get()->debug(
        "[Config] Loaded HookCrashers.ini. Overlay={}, external console={}",
        m_settings.enableOverlay, m_settings.showExternalConsole);
    return true;
}

} // namespace Config
} // namespace HookCrashers
