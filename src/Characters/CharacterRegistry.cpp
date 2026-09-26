#include "CharacterRegistry.h"

#include "../Core/Logger.h"
#include "../NativeDB/Classes.h"

namespace HookCrashers::Characters {

Registry& Registry::Instance() {
    static Registry registry;
    return registry;
}

bool Registry::RegisterCharacter(const AddonCharacter& character) {
    if (character.name.empty()) {
        Core::Logger::Error("Rejecting addon character with empty name");
        return false;
    }
    m_Addons.push_back(character);
    Core::Logger::Info("Character registered: index=%d name='%s' assets='%s' weapon=%u pet=%u initiallyUnlocked=%d freshOnly=%d",
                       FirstAddonIndex() + AddonCount() - 1,
                       character.name.c_str(),
                       character.assetDirectory.c_str(),
                       character.startingWeapon,
                       character.startingPet,
                       character.initiallyUnlocked ? 1 : 0,
                       character.freshOnly ? 1 : 0);
    return true;
}

void Registry::Clear() {
    Core::Logger::Info("Clearing character registry (%d addons)", AddonCount());
    m_Addons.clear();
}

const std::vector<AddonCharacter>& Registry::Addons() const {
    return m_Addons;
}

const AddonCharacter* Registry::AddonByBaseIndex(std::uint32_t baseIndex) const {
    if (baseIndex < FirstAddonIndex()) {
        return nullptr;
    }
    const std::uint32_t addonIndex = baseIndex - FirstAddonIndex();
    if (addonIndex >= m_Addons.size()) {
        return nullptr;
    }
    return &m_Addons[addonIndex];
}

int Registry::AddonCount() const {
    return static_cast<int>(m_Addons.size());
}

int Registry::ClassicAddonCount() const {
    int count = 0;
    for (const AddonCharacter& addon : m_Addons) {
        if (!addon.freshOnly) {
            ++count;
        }
    }
    return count;
}

int Registry::ClassicUnlockedAddonCount() const {
    int count = 0;
    for (const AddonCharacter& addon : m_Addons) {
        if (!addon.freshOnly && addon.initiallyUnlocked) {
            ++count;
        }
    }
    return count;
}

int Registry::ClassicVisibleCount() const {
    // This value is the exclusive upper bound scanned by the Classic character
    // selectors. Locked entries are filtered by IsCharacterUnlockedForPlayer.
    return static_cast<int>(NativeDB::kVanillaBaseCharacterCount) + AddonCount();
}

int Registry::FreshVisibleCount() const {
    int count = static_cast<int>(NativeDB::kVanillaBaseCharacterCount);
    for (const AddonCharacter& addon : m_Addons) {
        if (addon.initiallyUnlocked) {
            ++count;
        }
    }
    return count;
}

int Registry::FreshBaseCount() const {
    return static_cast<int>(NativeDB::kVanillaBaseCharacterCount) + AddonCount();
}

int Registry::NetworkBaseCount() const {
    return FreshBaseCount();
}

int Registry::NetworkTotalCount() const {
    return NetworkBaseCount() + static_cast<int>(NativeDB::kWorkshopNetworkSlotCount);
}

int Registry::SaveCharacterSlotCount() const {
    return FreshBaseCount() + static_cast<int>(NativeDB::kWorkshopSaveSlotCount);
}

int Registry::FirstAddonIndex() const {
    return static_cast<int>(NativeDB::kVanillaBaseCharacterCount);
}

int Registry::FirstAddonId() const {
    return FirstAddonIndex() + 1;
}

int Registry::FirstWorkshopId() const {
    return NetworkBaseCount() + 1;
}

int Registry::PastLastNetworkId() const {
    return NetworkTotalCount() + 1;
}

int Registry::SaveSize() const {
    return static_cast<int>(NativeDB::kSaveFileSize) + static_cast<int>(NativeDB::kCharacterDataSize) * AddonCount();
}

int Registry::SavePayloadSize() const {
    return static_cast<int>(NativeDB::kSavePayloadSize) + static_cast<int>(NativeDB::kCharacterDataSize) * AddonCount();
}

int Registry::KeybindsOffset() const {
    return static_cast<int>(NativeDB::kKeybindsOffset) + static_cast<int>(NativeDB::kCharacterDataSize) * AddonCount();
}

} // namespace HookCrashers::Characters
