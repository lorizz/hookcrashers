#pragma once

#include "../Config/HookCrashersConfig.h"
#include <vector>

namespace HookCrashers {
namespace Save {

static constexpr int GLOBAL_UNLOCK_SIZE = 64;
static constexpr int CHAR_DATA_SIZE = 48;
static constexpr int BASE_CLASSIC_COUNT = 31;
static constexpr int BASE_FRESH_COUNT = 32;
static constexpr int WORKSHOP_CHAR_COUNT = 10;
static constexpr int BASE_CHAR_COUNT = BASE_FRESH_COUNT;
static constexpr int TOTAL_ORIGINAL_CHARS = BASE_CHAR_COUNT + WORKSHOP_CHAR_COUNT;
static constexpr int BASE_SAVE_SIZE = 0x850;
static constexpr int BASE_SAVE_CAPACITY = 0x84C;
static constexpr int BASE_KEYBINDS_OFFSET = 0x820;
static constexpr int TOTAL_ORIGINAL_SLOTS = 73;
static constexpr int CHAR_TABLE_ENTRY_SIZE = 32;
static constexpr int CHAR_TABLE_BASE_SIZE = 14728;

class CharacterConfig {
public:
    static CharacterConfig& Instance();

    void LoadFromHookCrashersConfig();
    void RegisterCharacter(const Config::AddonCharacterDef& def);
    void ClearRegisteredCharacters();
    const std::vector<Config::AddonCharacterDef>& GetAddons() const { return m_addons; }
    int GetAddonCount() const { return static_cast<int>(m_addons.size()); }
    int GetExpandedCharacterCount() const { return BASE_CHAR_COUNT + GetAddonCount(); }
    int GetExpandedSafeCharacterCount() const { return TOTAL_ORIGINAL_CHARS + GetAddonCount(); }

private:
    std::vector<Config::AddonCharacterDef> m_addons;
};

} // namespace Save
} // namespace HookCrashers
