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
#include <unordered_map>
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

bool ParseQuotedString(const std::string& value, std::string& out);
bool ParseInt(const std::string& value, int& out);

std::string StripLuaLineComments(const std::string& text) {
    std::string out;
    out.reserve(text.size());

    bool inString = false;
    bool escape = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];

        if (escape) {
            out.push_back(ch);
            escape = false;
            continue;
        }

        if (ch == '\\' && inString) {
            out.push_back(ch);
            escape = true;
            continue;
        }

        if (ch == '"') {
            inString = !inString;
            out.push_back(ch);
            continue;
        }

        if (!inString && ch == '-' && i + 1 < text.size() && text[i + 1] == '-') {
            while (i < text.size() && text[i] != '\n') {
                ++i;
            }
            if (i < text.size()) {
                out.push_back(text[i]);
            }
            continue;
        }

        out.push_back(ch);
    }

    return out;
}

std::string NormalizeEnumToken(std::string value) {
    value = Trim(value);

    std::string quoted;
    if (ParseQuotedString(value, quoted)) {
        value = quoted;
    }

    const std::size_t lastDot = value.find_last_of('.');
    if (lastDot != std::string::npos) {
        value = value.substr(lastDot + 1);
    }

    std::string normalized;
    normalized.reserve(value.size());
    for (unsigned char ch : value) {
        if (ch == '_' || ch == '-' || std::isspace(ch) != 0) {
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(ch)));
    }
    return normalized;
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

std::string ReadManifestName(const std::string& manifestPath) {
    const std::string text = ReadTextFile(manifestPath);
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty() || std::isspace(static_cast<unsigned char>(line[0])) != 0) {
            continue;
        }

        const std::size_t colon = line.find(':');
        if (colon == std::string::npos || Trim(line.substr(0, colon)) != "name") {
            continue;
        }

        std::string value = Trim(line.substr(colon + 1));
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        } else {
            const std::size_t comment = value.find('#');
            if (comment != std::string::npos) {
                value = Trim(value.substr(0, comment));
            }
        }
        return value;
    }
    return {};
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

const std::unordered_map<std::string, int>& WeaponEnumMap() {
    static const std::unordered_map<std::string, int> values {
        {"none", 0},
        {"skinnysword", 2},
        {"thinsword", 3},
        {"thicksword", 4},
        {"pumpkinpeeler", 5},
        {"gladiatorsword", 6},
        {"butcherknife", 7},
        {"halfsword", 8},
        {"carrot", 9},
        {"thiefsword", 10},
        {"goldsword", 11},
        {"dualprongsword", 12},
        {"zigzag", 13},
        {"playdopastamaker", 14},
        {"falchion", 15},
        {"pointysword", 16},
        {"chewedupsword", 17},
        {"fencersfoil", 18},
        {"barbarianaxe", 19},
        {"pitchfork", 20},
        {"curvedsword", 21},
        {"keysword", 22},
        {"applepeeler", 23},
        {"rubberhandlesword", 24},
        {"mace", 25},
        {"club", 26},
        {"uglymace", 27},
        {"refinedmace", 28},
        {"fish", 29},
        {"wrappedsword", 30},
        {"skeletormace", 31},
        {"clunkymace", 32},
        {"snakeymace", 33},
        {"ratbeatingbat", 34},
        {"blackmorningstar", 35},
        {"kingsmace", 36},
        {"meattenderizer", 37},
        {"leaf", 38},
        {"sheathedsword", 39},
        {"practicefoil", 40},
        {"twig", 41},
        {"leafytwig", 42},
        {"lightsaber", 43},
        {"staff", 44},
        {"woodenspoon", 45},
        {"boneleg", 46},
        {"aliengun", 47},
        {"fishingspear", 48},
        {"lance", 49},
        {"sai", 50},
        {"unicornhorn", 51},
        {"ribeye", 52},
        {"kielbasa", 53},
        {"lobster", 54},
        {"umbrella", 55},
        {"broadax", 56},
        {"broadaxe", 56},
        {"evilsword", 57},
        {"icesword", 58},
        {"candlestick", 59},
        {"panicmallet", 60},
        {"fishingrod", 61},
        {"wrench", 62},
        {"nglollipop", 63},
        {"goldskullmace", 64},
        {"nggoldsword", 65},
        {"chainsaw", 66},
        {"broadspear", 67},
        {"glowstick", 68},
        {"chickenstick", 69},
        {"demonsword", 70},
        {"broccolisword", 71},
        {"mancatcher", 72},
        {"woodenmace", 73},
        {"ninjaclaw", 74},
        {"buffalomace", 75},
        {"electriceel", 76},
        {"scissors", 77},
        {"dinnerfork", 78},
        {"cattleprod", 79},
        {"lightningbolt", 80},
        {"twobyfour", 81},
        {"woodensword", 82},
        {"cardboardtube", 83},
        {"emeraldsword", 84},
        {"hammer", 85},
        {"pencil", 86},
        {"workshopdefault", 214},
        {"addondefault", 214},
    };
    return values;
}

const std::unordered_map<std::string, int>& PetEnumMap() {
    static const std::unordered_map<std::string, int> values {
        {"none", 0},
        {"cardinal", 1},
        {"owlet", 2},
        {"rammy", 3},
        {"frogglet", 4},
        {"monkeyface", 5},
        {"bipolarbear", 6},
        {"biteybat", 7},
        {"yeti", 8},
        {"troll", 9},
        {"snailburt", 10},
        {"giraffey", 11},
        {"zebra", 12},
        {"meowburt", 13},
        {"pazzo", 14},
        {"burlybear", 15},
        {"hawkster", 16},
        {"snoot", 17},
        {"piggy", 18},
        {"spiny", 19},
        {"scratchpaw", 20},
        {"seahorse", 21},
        {"chicken", 22},
        {"installball", 23},
        {"installbat", 23},
        {"mrbuddy", 24},
        {"sherbert", 25},
        {"pelter", 26},
        {"dragonhead", 27},
        {"beholder", 28},
        {"goldenwhale", 29},
    };
    return values;
}

bool ParseByteEnum(const std::string& value, const std::unordered_map<std::string, int>& enumMap, int& out) {
    if (ParseInt(value, out)) {
        return out >= 0 && out <= 255;
    }

    const std::string normalized = NormalizeEnumToken(value);
    const auto found = enumMap.find(normalized);
    if (found == enumMap.end()) {
        return false;
    }

    out = found->second;
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

bool ExecuteRegisterCharacter(const std::string& args,
                              const std::string& scriptPath,
                              const std::string& characterAssetsRoot,
                              int& charactersRegistered) {
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
        !ParseByteEnum(parsed[1], WeaponEnumMap(), weapon) ||
        !ParseByteEnum(parsed[2], PetEnumMap(), pet) ||
        !ParseBool(parsed[3], initiallyUnlocked) ||
        !ParseBool(parsed[4], freshOnly)) {
        Logger::Error("Failed to parse RegisterCharacter arguments in %s: name='%s' weapon='%s' pet='%s' initiallyUnlocked='%s' freshOnly='%s'",
                      scriptPath.c_str(),
                      parsed[0].c_str(),
                      parsed[1].c_str(),
                      parsed[2].c_str(),
                      parsed[3].c_str(),
                      parsed[4].c_str());
        return false;
    }

    Characters::AddonCharacter character;
    character.name = name;
    character.assetDirectory = JoinPath(characterAssetsRoot, name);
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

int ExecuteScript(const std::string& scriptPath, const std::string& modDirectory) {
    Logger::Info("Executing mod script: %s", scriptPath.c_str());
    const std::string rawText = ReadTextFile(scriptPath);
    if (rawText.empty()) {
        Logger::Warn("Script is empty or unreadable: %s", scriptPath.c_str());
        return 0;
    }

    const std::string text = StripLuaLineComments(rawText);
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

        ExecuteRegisterCharacter(text.substr(open + 1, close - open - 1),
                                 scriptPath,
                                 JoinPath(modDirectory, "characters"),
                                 registered);
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

    std::vector<std::string> modNames;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            std::strcmp(data.cFileName, ".") == 0 ||
            std::strcmp(data.cFileName, "..") == 0) {
            continue;
        }

        modNames.emplace_back(data.cFileName);
    } while (FindNextFileA(find, &data));
    FindClose(find);

    std::sort(modNames.begin(), modNames.end(), [](std::string left, std::string right) {
        std::transform(left.begin(), left.end(), left.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        std::transform(right.begin(), right.end(), right.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return left < right;
    });

    for (const std::string& modName : modNames) {

        ++result.modsFound;
        const std::string modDirectory = JoinPath(modsDirectory, modName);
        Logger::Info("Found mod: %s", modDirectory.c_str());

        std::vector<std::string> entrypoints;
        const std::string manifestPath = JoinPath(modDirectory, "manifest.yaml");
        if (FileExists(manifestPath)) {
            Logger::Info("Reading manifest: %s", manifestPath.c_str());
            const std::string manifestName = ReadManifestName(manifestPath);
            if (manifestName.empty()) {
                Logger::Warn("Manifest has no usable name: %s", manifestPath.c_str());
            } else if (result.manifestName.empty()) {
                result.manifestName = manifestName;
                Logger::Info("Using manifest name for save namespace: %s", manifestName.c_str());
            } else if (result.manifestName != manifestName) {
                Logger::Warn("Multiple manifest names found; save namespace remains '%s', ignoring '%s'",
                             result.manifestName.c_str(),
                             manifestName.c_str());
            }
            entrypoints = ReadManifestEntrypoints(manifestPath);
        } else {
            Logger::Warn("Mod has no manifest.yaml: %s", modDirectory.c_str());
        }

        if (entrypoints.empty()) {
            Logger::Warn("No manifest entrypoints found for %s, falling back to scripts/*.lua", modName.c_str());
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
            result.charactersRegistered += ExecuteScript(script, modDirectory);
        }
    }

    Logger::Info("Mod scan complete: mods=%d scripts=%d characters=%d manifestName='%s'",
                 result.modsFound,
                 result.scriptsRun,
                 result.charactersRegistered,
                 result.manifestName.c_str());
    return result;
}

} // namespace HookCrashers::Core::ModLoader
