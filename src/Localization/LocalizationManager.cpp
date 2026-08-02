#include "LocalizationManager.h"

#include "../Core/Logger.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace HookCrashers::Localization {
namespace {

void SkipWhitespace(const std::string& text, std::size_t& cursor) {
    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) ++cursor;
}

bool ParseString(const std::string& text, std::size_t& cursor, std::string& out) {
    SkipWhitespace(text, cursor);
    if (cursor >= text.size() || text[cursor++] != '"') return false;
    out.clear();
    while (cursor < text.size()) {
        const char ch = text[cursor++];
        if (ch == '"') return true;
        if (ch != '\\') {
            out.push_back(ch);
            continue;
        }
        if (cursor >= text.size()) return false;
        switch (const char escaped = text[cursor++]) {
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        default: out.push_back(escaped); break;
        }
    }
    return false;
}

bool Consume(const std::string& text, std::size_t& cursor, char expected) {
    SkipWhitespace(text, cursor);
    if (cursor >= text.size() || text[cursor] != expected) return false;
    ++cursor;
    return true;
}

using ParsedValues = std::map<std::string, std::map<std::string, std::string>>;

bool ParseLocalizationJson(const std::string& text, ParsedValues& values) {
    std::size_t cursor = 0;
    if (!Consume(text, cursor, '{')) return false;
    while (true) {
        SkipWhitespace(text, cursor);
        if (cursor < text.size() && text[cursor] == '}') return true;

        std::string language;
        if (!ParseString(text, cursor, language) || !Consume(text, cursor, ':') || !Consume(text, cursor, '{')) return false;
        auto& languageValues = values[language];
        while (true) {
            SkipWhitespace(text, cursor);
            if (cursor < text.size() && text[cursor] == '}') {
                ++cursor;
                break;
            }
            std::string id;
            std::string value;
            if (!ParseString(text, cursor, id) || !Consume(text, cursor, ':') || !ParseString(text, cursor, value)) return false;
            languageValues[id] = value;
            SkipWhitespace(text, cursor);
            if (cursor < text.size() && text[cursor] == ',') ++cursor;
        }
        SkipWhitespace(text, cursor);
        if (cursor < text.size() && text[cursor] == ',') ++cursor;
    }
}

std::wstring Widen(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), wide.data(), length);
    return wide;
}

} // namespace

LocalizationManager& LocalizationManager::Instance() {
    static LocalizationManager manager;
    return manager;
}

void LocalizationManager::Reset() {
    entries_.clear();
    indexById_.clear();
}

bool LocalizationManager::LoadFile(const std::string& path, const std::string& sourceName) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream stream;
    stream << file.rdbuf();

    ParsedValues parsed;
    if (!ParseLocalizationJson(stream.str(), parsed)) {
        Core::Logger::Error("Localization: invalid JSON '%s' mod='%s'", path.c_str(), sourceName.c_str());
        return false;
    }

    std::set<std::string> ids;
    for (const auto& language : parsed) {
        for (const auto& item : language.second) ids.insert(item.first);
    }

    std::size_t loaded = 0;
    for (const std::string& id : ids) {
        if (indexById_.find(id) != indexById_.end()) {
            Core::Logger::Warn("Localization: duplicate id '%s' ignored from mod '%s'", id.c_str(), sourceName.c_str());
            continue;
        }

        Entry entry;
        entry.logicalId = id;
        for (const auto& language : parsed) {
            const auto found = language.second.find(id);
            if (found == language.second.end()) continue;
            entry.utf8ByLanguage[language.first] = found->second;
            entry.wideByLanguage[language.first] = Widen(found->second);
        }
        if (entry.utf8ByLanguage.find("en") == entry.utf8ByLanguage.end()) {
            Core::Logger::Warn("Localization: id '%s' has no English fallback", id.c_str());
        }
        indexById_[id] = entries_.size();
        entries_.push_back(std::move(entry));
        ++loaded;
    }

    Core::Logger::Info("Localization: loaded %u ids from '%s' mod='%s' total=%u",
                       static_cast<unsigned>(loaded), path.c_str(), sourceName.c_str(), static_cast<unsigned>(entries_.size()));
    return true;
}

std::string LocalizationManager::CurrentLanguage() const {
    HMODULE steamApi = GetModuleHandleA("steam_api.dll");
    if (!steamApi) return "en";
    using SteamAppsFn = void*(__cdecl*)();
    using GetLanguageFn = const char*(__cdecl*)(void*);
    auto steamApps = reinterpret_cast<SteamAppsFn>(GetProcAddress(steamApi, "SteamAPI_SteamApps_v008"));
    auto getLanguage = reinterpret_cast<GetLanguageFn>(GetProcAddress(steamApi, "SteamAPI_ISteamApps_GetCurrentGameLanguage"));
    if (!steamApps || !getLanguage) return "en";
    const char* language = getLanguage(steamApps());
    if (!language) return "en";
    if (_stricmp(language, "german") == 0) return "de";
    if (_stricmp(language, "french") == 0) return "fr";
    if (_stricmp(language, "italian") == 0) return "it";
    if (_stricmp(language, "koreana") == 0) return "ko";
    if (_stricmp(language, "spanish") == 0) return "es";
    if (_stricmp(language, "schinese") == 0) return "zh_cn";
    if (_stricmp(language, "tchinese") == 0) return "zh_tw";
    if (_stricmp(language, "russian") == 0) return "ru";
    if (_stricmp(language, "japanese") == 0) return "ja";
    if (_stricmp(language, "portuguese") == 0) return "pt";
    return "en";
}

const LocalizationManager::Entry* LocalizationManager::Find(const std::string& logicalId) const {
    const auto found = indexById_.find(logicalId);
    return found == indexById_.end() ? nullptr : &entries_[found->second];
}

int LocalizationManager::NumericId(const std::string& logicalId) const {
    const auto found = indexById_.find(logicalId);
    return found == indexById_.end() ? -1 : static_cast<int>(BaseId() + found->second);
}

const wchar_t* LocalizationManager::StringForNumericId(std::uint32_t numericId) const {
    if (numericId < BaseId()) return nullptr;
    const std::size_t index = static_cast<std::size_t>(numericId - BaseId());
    if (index >= entries_.size()) return nullptr;
    const Entry& entry = entries_[index];
    const std::string language = CurrentLanguage();
    auto value = entry.wideByLanguage.find(language);
    if (value == entry.wideByLanguage.end()) value = entry.wideByLanguage.find("en");
    return value == entry.wideByLanguage.end() ? L"" : value->second.c_str();
}

bool LocalizationManager::Resolve(const std::string& logicalId, std::string& value) const {
    const Entry* entry = Find(logicalId);
    if (!entry) return false;
    const std::string language = CurrentLanguage();
    auto found = entry->utf8ByLanguage.find(language);
    if (found == entry->utf8ByLanguage.end()) found = entry->utf8ByLanguage.find("en");
    if (found == entry->utf8ByLanguage.end()) return false;
    value = found->second;
    return true;
}

std::size_t LocalizationManager::Count() const { return entries_.size(); }

} // namespace HookCrashers::Localization
