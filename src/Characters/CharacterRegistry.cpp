#include "CharacterRegistry.h"

#include "../NativeDB/Classes.h"

namespace HookCrashers::Characters {

Registry& Registry::Instance() {
    static Registry registry;
    return registry;
}

bool Registry::RegisterCharacter(const AddonCharacter& character) {
    if (character.name.empty()) {
        return false;
    }
    m_Addons.push_back(character);
    return true;
}

void Registry::Clear() {
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

int Registry::ClassicVisibleCount() const {
    return static_cast<int>(NativeDB::kClassicVisibleCharacterCount) + ClassicAddonCount();
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
