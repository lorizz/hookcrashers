#include "Config.h"

#include "Logger.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

namespace HookCrashers::Core::Config {
namespace {

std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) {
        return b;
    }
    if (a.back() == '\\' || a.back() == '/') {
        return a + b;
    }
    return a + "\\" + b;
}

std::string Trim(std::string value) {
    auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) {
        return !isSpace(static_cast<unsigned char>(ch));
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) {
        return !isSpace(static_cast<unsigned char>(ch));
    }).base(), value.end());
    return value;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string Unquote(std::string value) {
    value = Trim(value);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

bool FileExists(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void WriteDefaultConfig(const std::string& path) {
    std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file) {
        Logger::Error("Failed to create HookCrashers.yaml at '%s'", path.c_str());
        return;
    }

    file << "updates_enabled: true\n";
    file << "channel: stable\n";
    file << "testing_key: \"\"\n";
    file << "show_external_console: false\n";
    file << "enable_overlay: true\n";
    file << "overlay_toggle_key: Home\n";
}

void ApplyKeyValue(RuntimeConfig& config, const std::string& key, const std::string& rawValue) {
    const std::string normalizedKey = ToLower(Trim(key));
    const std::string value = Unquote(rawValue);
    const std::string normalizedValue = ToLower(value);

    if (normalizedKey == "updates_enabled") {
        config.updatesEnabled = normalizedValue != "false" && normalizedValue != "0" && normalizedValue != "no";
    } else if (normalizedKey == "channel") {
        config.channel = (normalizedValue == "test" || normalizedValue == "testing" || normalizedValue == "dev")
            ? UpdateChannel::Testing
            : UpdateChannel::Stable;
    } else if (normalizedKey == "testing_key") {
        config.testingKey = value;
    } else if (normalizedKey == "show_external_console" || normalizedKey == "showexternalconsole") {
        config.showExternalConsole = normalizedValue == "true" || normalizedValue == "1" || normalizedValue == "yes";
    } else if (normalizedKey == "enable_overlay" || normalizedKey == "enableoverlay") {
        config.enableOverlay = normalizedValue != "false" && normalizedValue != "0" && normalizedValue != "no";
    } else if (normalizedKey == "overlay_toggle_key" || normalizedKey == "overlaytogglekey") {
        if (normalizedValue == "home") config.overlayToggleKey = VK_HOME;
        else if (normalizedValue == "f10") config.overlayToggleKey = VK_F10;
        else if (normalizedValue == "f11") config.overlayToggleKey = VK_F11;
        else if (normalizedValue == "f12") config.overlayToggleKey = VK_F12;
    }
}

} // namespace

const char* ChannelName(UpdateChannel channel) {
    return channel == UpdateChannel::Testing ? "testing" : "stable";
}

RuntimeConfig LoadOrCreate(const std::string& moduleDirectory) {
    const std::string path = JoinPath(moduleDirectory, "HookCrashers.yaml");
    if (!FileExists(path)) {
        WriteDefaultConfig(path);
        Logger::Info("Created default HookCrashers.yaml");
    }

    RuntimeConfig config {};
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        Logger::Warn("Using default config; failed to read '%s'", path.c_str());
        return config;
    }

    std::string line;
    while (std::getline(file, line)) {
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }

        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        ApplyKeyValue(config, line.substr(0, colon), line.substr(colon + 1));
    }

    Logger::Info("Config loaded: updates=%d channel=%s testingKeyPresent=%d externalConsole=%d overlay=%d overlayKey=0x%02X",
                 config.updatesEnabled ? 1 : 0,
                 ChannelName(config.channel),
                 config.testingKey.empty() ? 0 : 1,
                 config.showExternalConsole ? 1 : 0,
                 config.enableOverlay ? 1 : 0,
                 config.overlayToggleKey);
    return config;
}

} // namespace HookCrashers::Core::Config

