#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace HookCrashers::Localization {

class LocalizationManager {
public:
    static LocalizationManager& Instance();

    void Reset();
    bool LoadFile(const std::string& path, const std::string& sourceName);
    int NumericId(const std::string& logicalId) const;
    const wchar_t* StringForNumericId(std::uint32_t numericId) const;
    bool Resolve(const std::string& logicalId, std::string& value) const;
    std::size_t Count() const;

    static constexpr std::uint32_t BaseId() { return 5000; }

private:
    struct Entry {
        std::string logicalId;
        std::unordered_map<std::string, std::string> utf8ByLanguage;
        std::unordered_map<std::string, std::wstring> wideByLanguage;
    };

    std::string CurrentLanguage() const;
    const Entry* Find(const std::string& logicalId) const;

    std::vector<Entry> entries_;
    std::unordered_map<std::string, std::size_t> indexById_;
};

} // namespace HookCrashers::Localization
