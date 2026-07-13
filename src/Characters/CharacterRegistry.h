#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace HookCrashers::Characters {

enum class Weapon : std::uint8_t {
    None = 0,
    WoodenSword = 3,
    WorkshopDefault = 214
};

enum class Pet : std::uint8_t {
    None = 0
};

struct AddonCharacter {
    std::string name;
    std::string assetDirectory;
    std::uint8_t startingWeapon = static_cast<std::uint8_t>(Weapon::None);
    std::uint8_t startingPet = static_cast<std::uint8_t>(Pet::None);
    bool initiallyUnlocked = false;
    bool freshOnly = false;
};

class Registry {
public:
    static Registry& Instance();

    bool RegisterCharacter(const AddonCharacter& character);
    void Clear();

    const std::vector<AddonCharacter>& Addons() const;
    const AddonCharacter* AddonByBaseIndex(std::uint32_t baseIndex) const;

    int AddonCount() const;
    int ClassicAddonCount() const;
    int ClassicVisibleCount() const;
    int FreshBaseCount() const;
    int NetworkBaseCount() const;
    int NetworkTotalCount() const;
    int SaveCharacterSlotCount() const;
    int FirstAddonIndex() const;
    int FirstAddonId() const;
    int FirstWorkshopId() const;
    int PastLastNetworkId() const;
    int SaveSize() const;
    int SavePayloadSize() const;
    int KeybindsOffset() const;

private:
    std::vector<AddonCharacter> m_Addons;
};

} // namespace HookCrashers::Characters
