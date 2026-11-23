#include "AutoUpdater.h"

#include "../Core/Logger.h"
#include "../Core/Version.h"

#include <windows.h>
#include <bcrypt.h>
#include <shlwapi.h>
#include <urlmon.h>
#include <wininet.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "wininet.lib")

namespace HookCrashers::Updater::AutoUpdater {
namespace {

extern "C" IMAGE_DOS_HEADER __ImageBase;

constexpr const char* kTestingKeySalt = "HookCrashers-v5-testing-channel";
constexpr const char* kTestingKeySha256 = "0c5cf2d507179fded9e68d25f894bf75ff348a471fa42a3a73c32c2b5c5b33e2";

struct UpdateManifest {
    std::string version;
    std::string channel;
    std::string assetUrl;
    std::string assetSha256;
    std::string notes;
};

// Updates are only ever installed from the official release mirror.
constexpr const char* kTrustedAssetPrefix = "https://github.com/lorizz/hookcrashers-releases/releases/download/";

UpdateManifest g_availableManifest;
std::string g_moduleDirectory;
bool g_hasAvailableUpdate = false;

std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) {
        return b;
    }
    if (a.back() == '\\' || a.back() == '/') {
        return a + b;
    }
    return a + "\\" + b;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string BytesToHex(const std::vector<unsigned char>& bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
        result.push_back(kHex[byte >> 4]);
        result.push_back(kHex[byte & 0x0F]);
    }
    return result;
}

bool Sha256Hex(const std::string& text, std::string& out) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD dataLength = 0;
    std::vector<unsigned char> object;
    std::vector<unsigned char> digest(32);

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return false;
    }

    bool ok = false;
    if (BCryptGetProperty(algorithm,
                          BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&objectLength),
                          sizeof(objectLength),
                          &dataLength,
                          0) == 0) {
        object.resize(objectLength);
        if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) == 0) {
            if (BCryptHashData(hash,
                               reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),
                               static_cast<ULONG>(text.size()),
                               0) == 0 &&
                BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0) {
                out = BytesToHex(digest);
                ok = true;
            }
        }
    }

    if (hash) {
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

bool IsTestingKeyValid(const std::string& key) {
    if (key.empty()) {
        return false;
    }

    std::string digest;
    if (!Sha256Hex(std::string(kTestingKeySalt) + key, digest)) {
        Core::Logger::Warn("Failed to hash testing update key");
        return false;
    }
    return digest == kTestingKeySha256;
}

bool RewriteInvalidTestingConfigToStable(const std::string& moduleDirectory) {
    const std::string configPath = JoinPath(moduleDirectory, "HookCrashers.yaml");
    std::ifstream input(configPath, std::ios::in | std::ios::binary);
    if (!input) {
        Core::Logger::Warn("Could not open HookCrashers.yaml to reset invalid testing channel");
        return false;
    }

    std::vector<std::string> lines;
    std::string line;
    bool wroteChannel = false;
    bool wroteTestingKey = false;
    while (std::getline(input, line)) {
        std::string trimmed = line;
        trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
            return std::isspace(ch) == 0;
        }));
        const std::string lower = ToLower(trimmed);
        if (lower.rfind("channel:", 0) == 0) {
            lines.emplace_back("channel: stable");
            wroteChannel = true;
        } else if (lower.rfind("testing_key:", 0) == 0) {
            lines.emplace_back("testing_key: \"\"");
            wroteTestingKey = true;
        } else {
            lines.push_back(std::move(line));
        }
    }

    if (!wroteChannel) {
        lines.emplace_back("channel: stable");
    }
    if (!wroteTestingKey) {
        lines.emplace_back("testing_key: \"\"");
    }

    std::ofstream output(configPath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!output) {
        Core::Logger::Warn("Could not write HookCrashers.yaml to reset invalid testing channel");
        return false;
    }

    for (const std::string& outLine : lines) {
        output << outLine << "\n";
    }
    Core::Logger::Warn("Invalid testing channel key; HookCrashers.yaml was reset to stable");
    return true;
}

std::string HttpGetText(const char* url) {
    HINTERNET internet = InternetOpenA("HookCrashers/5", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!internet) {
        return {};
    }

    constexpr DWORD kTimeoutMs = 5000;
    InternetSetOptionA(internet, INTERNET_OPTION_CONNECT_TIMEOUT, (LPVOID)&kTimeoutMs, sizeof(kTimeoutMs));
    InternetSetOptionA(internet, INTERNET_OPTION_SEND_TIMEOUT, (LPVOID)&kTimeoutMs, sizeof(kTimeoutMs));
    InternetSetOptionA(internet, INTERNET_OPTION_RECEIVE_TIMEOUT, (LPVOID)&kTimeoutMs, sizeof(kTimeoutMs));

    HINTERNET request = InternetOpenUrlA(internet,
                                         url,
                                         nullptr,
                                         0,
                                         INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE,
                                         0);
    if (!request) {
        InternetCloseHandle(internet);
        return {};
    }

    std::string result;
    std::array<char, 4096> buffer {};
    DWORD bytesRead = 0;
    constexpr std::size_t kMaxManifestBytes = 64 * 1024; // a channel manifest is < 1 KiB
    while (InternetReadFile(request, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead) && bytesRead != 0) {
        if (result.size() + bytesRead > kMaxManifestBytes) {
            Core::Logger::Warn("Update manifest exceeds %u bytes; ignored", static_cast<unsigned>(kMaxManifestBytes));
            result.clear();
            break;
        }
        result.append(buffer.data(), bytesRead);
    }

    InternetCloseHandle(request);
    InternetCloseHandle(internet);
    return result;
}

std::string JsonStringValue(const std::string& json, const char* key) {
    const std::string quotedKey = std::string("\"") + key + "\"";
    std::size_t keyPos = json.find(quotedKey);
    if (keyPos == std::string::npos) {
        return {};
    }
    std::size_t colon = json.find(':', keyPos + quotedKey.size());
    if (colon == std::string::npos) {
        return {};
    }
    std::size_t start = json.find('"', colon + 1);
    if (start == std::string::npos) {
        return {};
    }

    std::string value;
    bool escape = false;
    for (std::size_t i = start + 1; i < json.size(); ++i) {
        const char ch = json[i];
        if (escape) {
            value.push_back(ch);
            escape = false;
            continue;
        }
        if (ch == '\\') {
            escape = true;
            continue;
        }
        if (ch == '"') {
            return value;
        }
        value.push_back(ch);
    }
    return {};
}

bool ParseManifest(const std::string& json, UpdateManifest& manifest) {
    manifest.version = JsonStringValue(json, "version");
    manifest.channel = JsonStringValue(json, "channel");
    manifest.assetUrl = JsonStringValue(json, "asset_url");
    manifest.assetSha256 = ToLower(JsonStringValue(json, "asset_sha256"));
    manifest.notes = JsonStringValue(json, "notes");
    return !manifest.version.empty() && !manifest.assetUrl.empty();
}

bool IsNewerVersion(const std::string& current, const std::string& remote) {
    auto parse = [](const std::string& version) {
        std::vector<unsigned long> components;
        std::size_t cursor = 0;
        while (cursor < version.size() && components.size() < 4) {
            while (cursor < version.size() && !std::isdigit(static_cast<unsigned char>(version[cursor]))) {
                ++cursor;
            }
            if (cursor == version.size()) break;

            unsigned long value = 0;
            while (cursor < version.size() && std::isdigit(static_cast<unsigned char>(version[cursor]))) {
                const unsigned digit = static_cast<unsigned>(version[cursor] - '0');
                if (value > ((std::numeric_limits<unsigned long>::max)() - digit) / 10) {
                    value = (std::numeric_limits<unsigned long>::max)();
                } else {
                    value = value * 10 + digit;
                }
                ++cursor;
            }
            components.push_back(value);
        }
        return components;
    };

    const std::vector<unsigned long> currentParts = parse(current);
    const std::vector<unsigned long> remoteParts = parse(remote);
    if (remoteParts.empty()) return false;

    const std::size_t count = (std::max)(currentParts.size(), remoteParts.size());
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned long currentPart = i < currentParts.size() ? currentParts[i] : 0;
        const unsigned long remotePart = i < remoteParts.size() ? remoteParts[i] : 0;
        if (remotePart != currentPart) return remotePart > currentPart;
    }

    const bool currentIsPrerelease = current.find('-') != std::string::npos;
    const bool remoteIsPrerelease = remote.find('-') != std::string::npos;
    return currentIsPrerelease && !remoteIsPrerelease;
}

bool DownloadFile(const std::string& url, const std::string& destination) {
    HRESULT result = URLDownloadToFileA(nullptr, url.c_str(), destination.c_str(), 0, nullptr);
    return SUCCEEDED(result);
}

bool WriteUpdaterScript(const std::string& moduleDirectory,
                        const std::string& downloadedAsi,
                        const std::string& currentAsi,
                        const std::string& gameExecutable,
                        std::string& scriptPath) {
    scriptPath = JoinPath(moduleDirectory, "HookCrashersApplyUpdate.cmd");
    std::ofstream script(scriptPath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!script) {
        return false;
    }

    const DWORD pid = GetCurrentProcessId();
    script << "@echo off\r\n";
    script << "setlocal\r\n";
    script << ":wait\r\n";
    script << "tasklist /FI \"PID eq " << pid << "\" | find \"" << pid << "\" >NUL\r\n";
    script << "if not errorlevel 1 (\r\n";
    script << "  timeout /t 1 /nobreak >NUL\r\n";
    script << "  goto wait\r\n";
    script << ")\r\n";
    script << "set /a copyAttempts=0\r\n";
    script << ":copyUpdate\r\n";
    script << "copy /Y \"" << downloadedAsi << "\" \"" << currentAsi << "\" >NUL\r\n";
    script << "if not errorlevel 1 goto updateApplied\r\n";
    script << "set /a copyAttempts+=1\r\n";
    script << "if %copyAttempts% GEQ 10 exit /b 1\r\n";
    script << "timeout /t 1 /nobreak >NUL\r\n";
    script << "goto copyUpdate\r\n";
    script << ":updateApplied\r\n";
    script << "del \"" << downloadedAsi << "\" >NUL 2>NUL\r\n";
    script << "start \"\" /D \"" << moduleDirectory << "\" \"" << gameExecutable << "\"\r\n";
    script << "del \"%~f0\" >NUL 2>NUL\r\n";
    return true;
}

bool VerifyDownloadedAsset(const std::string& path, const std::string& expectedSha256) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) {
        Core::Logger::Error("Update verification: cannot read downloaded asset");
        return false;
    }
    const std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::string actual;
    if (data.empty() || !Sha256Hex(data, actual)) {
        Core::Logger::Error("Update verification: cannot hash downloaded asset");
        return false;
    }
    if (actual != expectedSha256) {
        Core::Logger::Error("Update verification failed: expected sha256=%s actual=%s",
                            expectedSha256.c_str(), actual.c_str());
        return false;
    }
    Core::Logger::Info("Update verification passed sha256=%s", actual.c_str());
    return true;
}

bool InstallUpdate(const std::string& moduleDirectory, const UpdateManifest& manifest) {
    if (manifest.assetUrl.rfind(kTrustedAssetPrefix, 0) != 0) {
        Core::Logger::Error("Refusing update from untrusted URL '%s'", manifest.assetUrl.c_str());
        return false;
    }
    if (manifest.assetSha256.size() != 64) {
        Core::Logger::Error("Refusing update: manifest has no valid asset_sha256");
        return false;
    }

    const std::string downloadedAsi = JoinPath(moduleDirectory, "HookCrashers.asi.update");
    if (!DownloadFile(manifest.assetUrl, downloadedAsi)) {
        Core::Logger::Error("Failed to download update asset '%s'", manifest.assetUrl.c_str());
        return false;
    }
    if (!VerifyDownloadedAsset(downloadedAsi, manifest.assetSha256)) {
        DeleteFileA(downloadedAsi.c_str());
        return false;
    }

    std::string modulePath(MAX_PATH, '\0');
    DWORD written = GetModuleFileNameA(
        reinterpret_cast<HMODULE>(&__ImageBase),
        modulePath.data(),
        static_cast<DWORD>(modulePath.size()));
    if (written == 0) {
        modulePath = JoinPath(moduleDirectory, "HookCrashers.asi");
    } else {
        modulePath.resize(written);
    }

    std::string gameExecutable(MAX_PATH, '\0');
    written = GetModuleFileNameA(
        nullptr, gameExecutable.data(), static_cast<DWORD>(gameExecutable.size()));
    if (written == 0) {
        Core::Logger::Error("Failed to locate Castle Crashers executable for updater restart");
        DeleteFileA(downloadedAsi.c_str());
        return false;
    }
    gameExecutable.resize(written);

    std::string scriptPath;
    if (!WriteUpdaterScript(
            moduleDirectory, downloadedAsi, modulePath, gameExecutable, scriptPath)) {
        Core::Logger::Error("Failed to write updater script");
        DeleteFileA(downloadedAsi.c_str());
        return false;
    }

    const auto launchResult = reinterpret_cast<INT_PTR>(
        ShellExecuteA(nullptr, "open", scriptPath.c_str(), nullptr, moduleDirectory.c_str(), SW_HIDE));
    if (launchResult <= 32) {
        Core::Logger::Error(
            "Failed to launch update script result=%lld", static_cast<long long>(launchResult));
        DeleteFileA(downloadedAsi.c_str());
        DeleteFileA(scriptPath.c_str());
        return false;
    }

    Core::Logger::Info(
        "Update %s downloaded; helper launched, exiting for replacement",
        manifest.version.c_str());
    ExitProcess(0);
    return true;
}

} // namespace

bool CheckForUpdates(const std::string& moduleDirectory,
                     const Core::Config::RuntimeConfig& config) {
    g_hasAvailableUpdate = false;
    g_availableManifest = {};
    g_moduleDirectory = moduleDirectory;

    if (!config.updatesEnabled) {
        Core::Logger::Info("AutoUpdater skipped: updates disabled");
        return false;
    }

    Core::Config::UpdateChannel channel = config.channel;
    if (channel == Core::Config::UpdateChannel::Testing &&
        !IsTestingKeyValid(config.testingKey)) {
        Core::Logger::Warn(
            "Testing update channel requested with invalid key; falling back to stable");
        RewriteInvalidTestingConfigToStable(moduleDirectory);
        channel = Core::Config::UpdateChannel::Stable;
    }

    const char* manifestUrl = channel == Core::Config::UpdateChannel::Testing
        ? Core::Version::kTestingManifestUrl
        : Core::Version::kStableManifestUrl;

    Core::Logger::Info("Checking updates: current=%s channel=%s manifest=%s",
                       Core::Version::kVersion,
                       Core::Config::ChannelName(channel),
                       manifestUrl);

    UpdateManifest manifest {};
    const std::string json = HttpGetText(manifestUrl);
    if (json.empty() || !ParseManifest(json, manifest)) {
        Core::Logger::Warn("AutoUpdater could not read update manifest");
        return false;
    }

    if (!IsNewerVersion(Core::Version::kVersion, manifest.version)) {
        Core::Logger::Info("HookCrashers is up to date: %s", Core::Version::kVersion);
        return false;
    }

    g_availableManifest = std::move(manifest);
    g_hasAvailableUpdate = true;
    Core::Logger::Info("HookCrashers update available: current=%s remote=%s",
                       Core::Version::kVersion,
                       g_availableManifest.version.c_str());
    return true;
}

bool HasAvailableUpdate() {
    return g_hasAvailableUpdate;
}

bool InstallAvailableUpdate() {
    if (!g_hasAvailableUpdate || g_moduleDirectory.empty()) {
        Core::Logger::Warn("InstallAvailableUpdate called without an available update");
        return false;
    }
    return InstallUpdate(g_moduleDirectory, g_availableManifest);
}

} // namespace HookCrashers::Updater::AutoUpdater
