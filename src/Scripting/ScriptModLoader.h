#pragma once

#include <string>
#include <vector>

namespace HookCrashers {
namespace Scripting {

struct ScriptModInfo {
    std::string name;
    std::string author = "Unknown";
    std::string version = "0.0.0";
    std::string directory;
    std::string mainLuaPath;
    std::string localizationPath;
    std::string manifestPath;
    std::string iconPath;
    bool hasMainLua = false;
    bool hasLocalizations = false;
    bool hasManifest = false;
    bool hasIcon = false;
    size_t registeredCharacterCount = 0;
    size_t registeredLocalizationCount = 0;
    std::vector<std::string> registeredCharacterIds;
    std::vector<std::string> registeredLocalizationIds;
};

class ScriptModLoader {
public:
    static ScriptModLoader& Instance();

    bool DiscoverAndLoad();
    const std::vector<ScriptModInfo>& GetLoadedMods() const { return m_loadedMods; }

private:
    bool LoadModDirectory(const std::string& modRoot, const std::string& folderName);
    bool ParseManifestJson(const std::string& path, ScriptModInfo& modInfo);
    bool ParseMainLua(const std::string& path, ScriptModInfo& modInfo);
    bool ParseLocsJson(const std::string& path, ScriptModInfo& modInfo);

    std::vector<ScriptModInfo> m_loadedMods;
};

} // namespace Scripting
} // namespace HookCrashers
