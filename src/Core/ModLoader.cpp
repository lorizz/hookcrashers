#include "ModLoader.h"

#include "Logger.h"
#include "../Characters/CharacterRegistry.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace HookCrashers::Core::ModLoader {
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
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) { return !isSpace(static_cast<unsigned char>(ch)); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) { return !isSpace(static_cast<unsigned char>(ch)); }).base(), value.end());
    return value;
}

bool FileExists(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::string ReadTextFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

std::vector<std::string> ReadManifestEntrypoints(const std::string& manifestPath) {
    std::vector<std::string> entrypoints;
    const std::string text = ReadTextFile(manifestPath);
    if (text.empty()) {
        return entrypoints;
    }

    bool inEntrypoints = false;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        if (trimmed == "entrypoints:") {
            inEntrypoints = true;
            continue;
        }

        if (inEntrypoints) {
            if (trimmed.rfind("- ", 0) == 0) {
                entrypoints.push_back(Trim(trimmed.substr(2)));
                continue;
            }
            if (trimmed.find(':') != std::string::npos) {
                inEntrypoints = false;
            }
        }
    }

    return entrypoints;
}

std::vector<std::string> SplitArguments(const std::string& arguments) {
    std::vector<std::string> result;
    std::string current;
    bool inString = false;
    bool escape = false;

    for (char ch : arguments) {
        if (escape) {
            current.push_back(ch);
            escape = false;
            continue;
        }
        if (ch == '\\' && inString) {
            escape = true;
            current.push_back(ch);
            continue;
        }
        if (ch == '"') {
            inString = !inString;
            current.push_back(ch);
            continue;
        }
        if (ch == ',' && !inString) {
            result.push_back(Trim(current));
            current.clear();
            continue;
        }
        current.push_back(ch);
    }

    if (!current.empty()) {
        result.push_back(Trim(current));
    }
    return result;
}

bool ParseQuotedString(const std::string& value, std::string& out) {
    const std::string trimmed = Trim(value);
    if (trimmed.size() < 2 || trimmed.front() != '"' || trimmed.back() != '"') {
        return false;
    }

    out.clear();
    bool escape = false;
    for (std::size_t i = 1; i + 1 < trimmed.size(); ++i) {
        const char ch = trimmed[i];
        if (escape) {
            out.push_back(ch);
            escape = false;
            continue;
        }
        if (ch == '\\') {
            escape = true;
            continue;
        }
        out.push_back(ch);
    }
    return true;
}

bool ParseBool(const std::string& value, bool& out) {
    const std::string trimmed = Trim(value);
    if (trimmed == "true") {
        out = true;
        return true;
    }
    if (trimmed == "false") {
        out = false;
        return true;
    }
    return false;
}

bool ParseInt(const std::string& value, int& out) {
    const std::string trimmed = Trim(value);
    char* end = nullptr;
    const long parsed = std::strtol(trimmed.c_str(), &end, 0);
    if (!end || end == trimmed.c_str() || *end != '\0') {
        return false;
    }
    out = static_cast<int>(parsed);
    return true;
}

bool ExecuteRegisterCharacter(const std::string& args, const std::string& scriptPath, int& charactersRegistered) {
    const std::vector<std::string> parsed = SplitArguments(args);
    if (parsed.size() != 5) {
        Logger::Error("Invalid RegisterCharacter call in %s: expected 5 args, got %u", scriptPath.c_str(), static_cast<unsigned int>(parsed.size()));
        return false;
    }

    std::string name;
    int weapon = 0;
    int pet = 0;
    bool initiallyUnlocked = false;
    bool freshOnly = false;
    if (!ParseQuotedString(parsed[0], name) ||
        !ParseInt(parsed[1], weapon) ||
        !ParseInt(parsed[2], pet) ||
        !ParseBool(parsed[3], initiallyUnlocked) ||
        !ParseBool(parsed[4], freshOnly)) {
        Logger::Error("Failed to parse RegisterCharacter arguments in %s", scriptPath.c_str());
        return false;
    }

    Characters::AddonCharacter character;
    character.name = name;
    character.startingWeapon = static_cast<std::uint8_t>(weapon);
    character.startingPet = static_cast<std::uint8_t>(pet);
    character.initiallyUnlocked = initiallyUnlocked;
    character.freshOnly = freshOnly;

    if (!Characters::Registry::Instance().RegisterCharacter(character)) {
        Logger::Error("RegisterCharacter failed for '%s' in %s", name.c_str(), scriptPath.c_str());
        return false;
    }

    ++charactersRegistered;
    Logger::Info("Registered addon character '%s' weapon=%d pet=%d initiallyUnlocked=%d freshOnly=%d",
                 name.c_str(),
                 weapon,
                 pet,
                 initiallyUnlocked ? 1 : 0,
                 freshOnly ? 1 : 0);
    return true;
}

int ExecuteScript(const std::string& scriptPath) {
    Logger::Info("Executing mod script: %s", scriptPath.c_str());
    const std::string text = ReadTextFile(scriptPath);
    if (text.empty()) {
        Logger::Warn("Script is empty or unreadable: %s", scriptPath.c_str());
        return 0;
    }

    int registered = 0;
    std::size_t cursor = 0;
    constexpr const char* callName = "RegisterCharacter";
    while ((cursor = text.find(callName, cursor)) != std::string::npos) {
        const std::size_t open = text.find('(', cursor);
        if (open == std::string::npos) {
            break;
        }

        bool inString = false;
        int depth = 0;
        std::size_t close = std::string::npos;
        for (std::size_t i = open; i < text.size(); ++i) {
            const char ch = text[i];
            if (ch == '"' && (i == 0 || text[i - 1] != '\\')) {
                inString = !inString;
            }
            if (inString) {
                continue;
            }
            if (ch == '(') {
                ++depth;
            } else if (ch == ')') {
                --depth;
                if (depth == 0) {
                    close = i;
                    break;
                }
            }
        }

        if (close == std::string::npos) {
            Logger::Error("Unclosed RegisterCharacter call in %s", scriptPath.c_str());
            break;
        }

        ExecuteRegisterCharacter(text.substr(open + 1, close - open - 1), scriptPath, registered);
        cursor = close + 1;
    }

    return registered;
}

std::vector<std::string> EnumerateLuaScripts(const std::string& scriptsDirectory) {
    std::vector<std::string> scripts;
    WIN32_FIND_DATAA data {};
    const std::string pattern = JoinPath(scriptsDirectory, "*.lua");
    HANDLE find = FindFirstFileA(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return scripts;
    }

    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            scripts.push_back(JoinPath(scriptsDirectory, data.cFileName));
        }
    } while (FindNextFileA(find, &data));

    FindClose(find);
    return scripts;
}

} // namespace

LoadResult LoadAll(const std::string& rootDirectory) {
    LoadResult result;
    const std::string modsDirectory = JoinPath(rootDirectory, "mods");
    Logger::Info("Scanning mods directory: %s", modsDirectory.c_str());

    WIN32_FIND_DATAA data {};
    const std::string pattern = JoinPath(modsDirectory, "*");
    HANDLE find = FindFirstFileA(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        Logger::Warn("Mods directory not found or empty: %s", modsDirectory.c_str());
        return result;
    }

    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            std::strcmp(data.cFileName, ".") == 0 ||
            std::strcmp(data.cFileName, "..") == 0) {
            continue;
        }

        ++result.modsFound;
        const std::string modDirectory = JoinPath(modsDirectory, data.cFileName);
        Logger::Info("Found mod: %s", modDirectory.c_str());

        std::vector<std::string> entrypoints;
        const std::string manifestPath = JoinPath(modDirectory, "manifest.yaml");
        if (FileExists(manifestPath)) {
            Logger::Info("Reading manifest: %s", manifestPath.c_str());
            entrypoints = ReadManifestEntrypoints(manifestPath);
        } else {
            Logger::Warn("Mod has no manifest.yaml: %s", modDirectory.c_str());
        }

        if (entrypoints.empty()) {
            Logger::Warn("No manifest entrypoints found for %s, falling back to scripts/*.lua", data.cFileName);
            entrypoints = EnumerateLuaScripts(JoinPath(modDirectory, "scripts"));
        } else {
            for (std::string& entrypoint : entrypoints) {
                entrypoint = JoinPath(modDirectory, entrypoint);
            }
        }

        for (const std::string& script : entrypoints) {
            if (!FileExists(script)) {
                Logger::Error("Entrypoint does not exist: %s", script.c_str());
                continue;
            }
            ++result.scriptsRun;
            result.charactersRegistered += ExecuteScript(script);
        }
    } while (FindNextFileA(find, &data));

    FindClose(find);
    Logger::Info("Mod scan complete: mods=%d scripts=%d characters=%d",
                 result.modsFound,
                 result.scriptsRun,
                 result.charactersRegistered);
    return result;
}

} // namespace HookCrashers::Core::ModLoader
