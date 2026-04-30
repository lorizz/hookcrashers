#include "LocalizationManager.h"
#include <fstream>
#include <thread>
#include <chrono>
#include <codecvt>
#include <locale>
#include "../Util/Logger.h"

namespace HookCrashers::Localization {

    LocalizationManager& LocalizationManager::getInstance() {
        static LocalizationManager instance;
        return instance;
    }

    LocalizationManager::LocalizationManager() {}

    std::wstring LocalizationManager::ToWString(const std::string& str) {
        if (str.empty()) return std::wstring();
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstrTo(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
        return wstrTo;
    }

    int LocalizationManager::getNumericId(const std::string& logicalId) {
        auto it = m_idMap.find(logicalId);
        return (it != m_idMap.end()) ? it->second : -1;
    }

    const wchar_t* LocalizationManager::getStringByIndex(int index) const {
        if (index >= 0 && static_cast<size_t>(index) < m_customStrings.size()) {
            const auto& entry = m_customStrings[static_cast<size_t>(index)];
            if (m_languageIndex >= 0 && static_cast<size_t>(m_languageIndex) < entry.languages.size()) {
                return entry.languages[m_languageIndex].c_str();
            }
        }
        return L"";
    }

    void LocalizationManager::Reset() {
        m_isInitialized = false;
        m_languageIndex = 0;
        m_baseCustomId = 5000;
        m_customStrings.clear();
        m_idMap.clear();
    }

    bool LocalizationManager::initialize(int baseCustomId) {
        if (m_isInitialized) return true;
        m_baseCustomId = baseCustomId;

        uintptr_t steamClientBase = (uintptr_t)GetModuleHandleA("steamclient.dll");
        if (steamClientBase == 0) {
            HookCrashers::Util::Logger::Instance().Get()->error("[CustomLocalizations] steamclient.dll not found!");
            return false;
        }

        const uintptr_t LANGUAGE_ADDRESS = steamClientBase + 0x12AFE80;
        std::string language;

        for (int attempts = 0; attempts < 600; ++attempts) {
            if (*(char*)LANGUAGE_ADDRESS != '\0') {
                language = (char*)LANGUAGE_ADDRESS;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (language.empty()) language = "english";

        HookCrashers::Util::Logger::Instance().Get()->info("[Localization] Detected Steam language '{}'.", language);

        if (language == "english") m_languageIndex = 0;
        else if (language == "german") m_languageIndex = 1;
        else if (language == "french") m_languageIndex = 2;
        else if (language == "spanish") m_languageIndex = 3;
        else if (language == "italian") m_languageIndex = 4;
        else if (language == "schinese") m_languageIndex = 5;
        else if (language == "koreana") m_languageIndex = 6;
        else if (language == "tchinese") m_languageIndex = 7;
        else if (language == "portuguese") m_languageIndex = 8;
        else if (language == "japanese") m_languageIndex = 9;
        else if (language == "russian") m_languageIndex = 10;
        else m_languageIndex = 0;

        m_isInitialized = true;
        return true;
    }

    size_t LocalizationManager::LoadJsonFile(const std::string& jsonPath, const std::string& sourceName, std::vector<std::string>* loadedIds) {
        if (!m_isInitialized) {
            HookCrashers::Util::Logger::Instance().Get()->error(
                "[Localization] Refused to load '{}' for mod '{}' because the manager is not initialized.",
                jsonPath,
                sourceName);
            return 0;
        }

        std::ifstream file(jsonPath);
        if (!file.is_open()) {
            HookCrashers::Util::Logger::Instance().Get()->error(
                "[Localization] Failed to open '{}' for mod '{}'.",
                jsonPath,
                sourceName);
            return 0;
        }

        try {
            nlohmann::json root;
            file >> root;
            size_t loadedCount = 0;

            if (root.contains("strings")) {
                for (const auto& item : root["strings"]) {
                    CustomLocalizationEntry entry;

                    if (item.contains("unlocalized_name")) {
                        entry.logicalId = item.at("unlocalized_name").get<std::string>();
                    }
                    else {
                        entry.logicalId = item.at("id").get<std::string>();
                    }
                    entry.numericId = m_baseCustomId + m_customStrings.size();

                    entry.languages.push_back(ToWString(item.value("english", "")));
                    entry.languages.push_back(ToWString(item.value("german", "")));
                    entry.languages.push_back(ToWString(item.value("french", "")));
                    entry.languages.push_back(ToWString(item.value("spanish", "")));
                    entry.languages.push_back(ToWString(item.value("italian", "")));
                    entry.languages.push_back(ToWString(item.value("schinese", "")));
                    entry.languages.push_back(ToWString(item.value("korean", "")));
                    entry.languages.push_back(ToWString(item.value("tchinese", "")));
                    entry.languages.push_back(ToWString(item.value("portuguese", "")));
                    entry.languages.push_back(ToWString(item.value("japanese", "")));
                    entry.languages.push_back(ToWString(item.value("russian", "")));

                    m_customStrings.push_back(entry);
                    m_idMap[entry.logicalId] = entry.numericId;
                    if (loadedIds) {
                        loadedIds->push_back(entry.logicalId);
                    }
                    HookCrashers::Util::Logger::Instance().Get()->info(
                        "[Localization] Loaded '{}' from mod '{}' -> numeric_id={} source='{}'.",
                        entry.logicalId,
                        sourceName,
                        entry.numericId,
                        jsonPath);
                    ++loadedCount;
                }
            }
            HookCrashers::Util::Logger::Instance().Get()->info(
                "[Localization] Loaded {} entries from '{}' for mod '{}'.",
                loadedCount,
                jsonPath,
                sourceName);
            return loadedCount;
        }
        catch (const std::exception& e) {
            HookCrashers::Util::Logger::Instance().Get()->error(
                "[Localization] Failed to parse '{}' for mod '{}': {}",
                jsonPath,
                sourceName,
                e.what());
            return 0;
        }
    }
}
