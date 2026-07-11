#include "SaveLayoutGuard.h"

#include "../Characters/CharacterRegistry.h"
#include "../Core/Logger.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace HookCrashers::Save::SaveLayoutGuard {
namespace {

constexpr int kSaveLayoutVersion = 3;

std::string TrimTrailingSlash(std::string path) {
    while (!path.empty() && (path.back() == '\\' || path.back() == '/')) {
        path.pop_back();
    }
    return path;
}

std::string ParentDirectory(const std::string& path) {
    const std::size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) {
        return {};
    }
    return path.substr(0, slash);
}

std::string FileNameOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool StartsWithCcSave(const std::string& fileName) {
    const std::string lower = ToLower(fileName);
    return lower.rfind("cc_save", 0) == 0;
}

bool ContainsCastleRemotePath(const std::string& path) {
    const std::string lower = ToLower(path);
    return lower.find("\\204360\\remote\\") != std::string::npos ||
           lower.find("/204360/remote/") != std::string::npos;
}

bool DirectoryExists(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureDirectory(const std::string& path) {
    if (path.empty() || DirectoryExists(path)) {
        return true;
    }

    const std::string parent = ParentDirectory(path);
    if (!parent.empty() && parent != path && !EnsureDirectory(parent)) {
        return false;
    }

    if (CreateDirectoryA(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) {
        return true;
    }

    Core::Logger::Error("SaveLayoutGuard failed to create directory '%s' error=%lu",
                        path.c_str(),
                        GetLastError());
    return false;
}

std::string Timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm local {};
    localtime_s(&local, &now);

    char buffer[32] {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%04d%02d%02d_%02d%02d%02d",
                  local.tm_year + 1900,
                  local.tm_mon + 1,
                  local.tm_mday,
                  local.tm_hour,
                  local.tm_min,
                  local.tm_sec);
    return buffer;
}

std::string BuildSignature() {
    const auto& registry = Characters::Registry::Instance();

    std::ostringstream stream;
    stream << "layoutVersion=" << kSaveLayoutVersion << "\n";
    stream << "addonCount=" << registry.AddonCount() << "\n";
    stream << "classicVisibleCount=" << registry.ClassicVisibleCount() << "\n";
    stream << "freshBaseCount=" << registry.FreshBaseCount() << "\n";
    stream << "networkBaseCount=" << registry.NetworkBaseCount() << "\n";
    stream << "networkTotalCount=" << registry.NetworkTotalCount() << "\n";
    stream << "saveCharacterSlotCount=" << registry.SaveCharacterSlotCount() << "\n";
    stream << "saveSize=" << registry.SaveSize() << "\n";
    stream << "savePayloadSize=" << registry.SavePayloadSize() << "\n";
    stream << "keybindsOffset=" << registry.KeybindsOffset() << "\n";
    return stream.str();
}

bool ReadTextFile(const std::string& path, std::string& output) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    output = buffer.str();
    return true;
}

bool WriteTextFile(const std::string& path, const std::string& content) {
    std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file) {
        Core::Logger::Error("SaveLayoutGuard failed to write marker '%s'", path.c_str());
        return false;
    }

    file << content;
    return true;
}

void FindSaveFilesRecursive(const std::string& directory, std::vector<std::string>& saveFiles) {
    const std::string search = TrimTrailingSlash(directory) + "\\*";
    WIN32_FIND_DATAA data {};
    HANDLE find = FindFirstFileA(search.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        const char* name = data.cFileName;
        if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) {
            continue;
        }

        const std::string path = TrimTrailingSlash(directory) + "\\" + name;
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            FindSaveFilesRecursive(path, saveFiles);
            continue;
        }

        if (StartsWithCcSave(name) && ContainsCastleRemotePath(path)) {
            saveFiles.push_back(path);
        }
    } while (FindNextFileA(find, &data));

    FindClose(find);
}

std::string SteamRootFromModuleDirectory(const std::string& moduleDirectory) {
    std::string current = TrimTrailingSlash(moduleDirectory);
    for (int depth = 0; depth < 8 && !current.empty(); ++depth) {
        if (DirectoryExists(current + "\\steamapps") && DirectoryExists(current + "\\userdata")) {
            return current;
        }
        current = ParentDirectory(current);
    }
    return {};
}

void BackupSaveFiles(const std::string& moduleDirectory) {
    const std::string steamRoot = SteamRootFromModuleDirectory(moduleDirectory);
    if (steamRoot.empty()) {
        Core::Logger::Warn("SaveLayoutGuard could not locate Steam root from '%s'", moduleDirectory.c_str());
        return;
    }

    const std::string userdata = steamRoot + "\\userdata";
    std::vector<std::string> saveFiles;
    FindSaveFilesRecursive(userdata, saveFiles);

    if (saveFiles.empty()) {
        Core::Logger::Info("SaveLayoutGuard found no existing cc_save files to invalidate");
        return;
    }

    const std::string backupDirectory = TrimTrailingSlash(moduleDirectory) + "\\save_backups\\hc5_runtime_" + Timestamp();
    if (!EnsureDirectory(backupDirectory)) {
        Core::Logger::Error("SaveLayoutGuard cannot backup saves; leaving originals untouched");
        return;
    }

    for (std::size_t i = 0; i < saveFiles.size(); ++i) {
        const std::string& source = saveFiles[i];
        std::ostringstream destinationName;
        destinationName << backupDirectory << "\\" << i << "_" << FileNameOf(source);
        const std::string destination = destinationName.str();

        if (MoveFileExA(source.c_str(), destination.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
            Core::Logger::Warn("SaveLayoutGuard invalidated save '%s' -> '%s'",
                               source.c_str(),
                               destination.c_str());
        } else {
            Core::Logger::Error("SaveLayoutGuard failed to move save '%s' error=%lu",
                                source.c_str(),
                                GetLastError());
        }
    }
}

} // namespace

void InvalidateIfNeeded(const std::string& moduleDirectory) {
    const auto& registry = Characters::Registry::Instance();
    if (registry.AddonCount() <= 0) {
        Core::Logger::Info("SaveLayoutGuard skipped: no addon characters registered");
        return;
    }

    const std::string markerPath = TrimTrailingSlash(moduleDirectory) + "\\HookCrashers.save_layout";
    const std::string expectedSignature = BuildSignature();

    std::string currentSignature;
    if (ReadTextFile(markerPath, currentSignature) && currentSignature == expectedSignature) {
        Core::Logger::Info("SaveLayoutGuard layout unchanged; marker is current");
        return;
    }

    Core::Logger::Warn("SaveLayoutGuard detected save layout change; invalidating old saves");
    Core::Logger::Info("SaveLayoutGuard signature:\n%s", expectedSignature.c_str());

    BackupSaveFiles(moduleDirectory);
    WriteTextFile(markerPath, expectedSignature);
}

} // namespace HookCrashers::Save::SaveLayoutGuard
