#pragma once

#include <string>
#include <vector>
#include <map>
#include <windows.h>
#include <json.hpp>
#include "LocalizedString.h"

namespace HookCrashers::Localization {

    class LocalizationManager {
    private:
        LocalizationManager();
        LocalizationManager(const LocalizationManager&) = delete;
        LocalizationManager& operator=(const LocalizationManager&) = delete;

        std::wstring ToWString(const std::string& str);

        bool m_isInitialized = false;
        int m_languageIndex = 0;
        int m_baseCustomId = 5000;

        // Vettore che contiene le traduzioni
        std::vector<CustomLocalizationEntry> m_customStrings;
        // Mappa da ID logico a ID numerico
        std::map<std::string, int> m_idMap;

    public:
        static LocalizationManager& getInstance();

        bool initialize(int baseCustomId);
        size_t LoadJsonFile(const std::string& jsonPath, const std::string& sourceName, std::vector<std::string>* loadedIds = nullptr);
        void Reset();

        int getNumericId(const std::string& logicalId);
        const wchar_t* getStringByIndex(int index) const;
        int getBaseCustomId() const { return m_baseCustomId; }

        bool isInitialized() const { return m_isInitialized; }
    };
}
