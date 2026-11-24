#pragma once

#include <cstdint>

#if defined(HOOKCRASHERS_EXPORTS)
#define HOOKCRASHERS_API __declspec(dllexport)
#else
#define HOOKCRASHERS_API __declspec(dllimport)
#endif

#define HOOKCRASHERS_MOD_EXPORT extern "C" __declspec(dllexport)

#include "CurrentCharacterData.h"

#if defined(_MSC_VER) && !defined(HOOKCRASHERS_NO_AUTOLINK)
#pragma comment(lib, "HookCrashers.lib")
#endif

namespace HookCrashers::Enums {

enum class Weapon : std::uint8_t {
    SkinnySword = 0x02,
    ThinSword = 0x03,
    ThickSword = 0x04,
    PumpkinPeeler = 0x05,
    GladiatorSword = 0x06,
    ButcherKnife = 0x07,
    HalfSword = 0x08,
    Carrot = 0x09,
    ThiefSword = 0x0A,
    GoldSword = 0x0B,
    DualProngSword = 0x0C,
    Zigzag = 0x0D,
    PlaydoPastaMaker = 0x0E,
    Falchion = 0x0F,
    PointySword = 0x10,
    ChewedUpSword = 0x11,
    FencersFoil = 0x12,
    BarbarianAxe = 0x13,
    Pitchfork = 0x14,
    CurvedSword = 0x15,
    KeySword = 0x16,
    ApplePeeler = 0x17,
    RubberHandleSword = 0x18,
    Mace = 0x19,
    Club = 0x1A,
    UglyMace = 0x1B,
    RefinedMace = 0x1C,
    Fish = 0x1D,
    WrappedSword = 0x1E,
    SkeletorMace = 0x1F,
    ClunkyMace = 0x20,
    SnakeyMace = 0x21,
    RatBeatingBat = 0x22,
    BlackMorningStar = 0x23,
    KingsMace = 0x24,
    MeatTenderizer = 0x25,
    Leaf = 0x26,
    SheathedSword = 0x27,
    PracticeFoil = 0x28,
    Twig = 0x29,
    LeafyTwig = 0x2A,
    LightSaber = 0x2B,
    Staff = 0x2C,
    WoodenSpoon = 0x2D,
    BoneLeg = 0x2E,
    AlienGun = 0x2F,
    FishingSpear = 0x30,
    Lance = 0x31,
    Sai = 0x32,
    UnicornHorn = 0x33,
    Ribeye = 0x34,
    Kielbasa = 0x35,
    Lobster = 0x36,
    Umbrella = 0x37,
    BroadAx = 0x38,
    EvilSword = 0x39,
    IceSword = 0x3A,
    Candlestick = 0x3B,
    PanicMallet = 0x3C,
    FishingRod = 0x3D,
    Wrench = 0x3E,
    NGLollipop = 0x3F,
    GoldSkullMace = 0x40,
    NGGoldSword = 0x41,
    Chainsaw = 0x42,
    BroadSpear = 0x43,
    Glowstick = 0x44,
    ChickenStick = 0x45,
    DemonSword = 0x46,
    BroccoliSword = 0x47,
    ManCatcher = 0x48,
    WoodenMace = 0x49,
    NinjaClaw = 0x4A,
    BuffaloMace = 0x4B,
    ElectricEel = 0x4C,
    Scissors = 0x4D,
    DinnerFork = 0x4E,
    CattleProd = 0x4F,
    LightningBolt = 0x50,
    TwoByFour = 0x51,
    WoodenSword = 0x52,
    CardboardTube = 0x53,
    EmeraldSword = 0x54,
    Hammer = 0x55,
    Pencil = 0x56
};

enum class Pet : std::uint8_t {
    None = 0x00,
    Cardinal = 0x01,
    Owlet = 0x02,
    Rammy = 0x03,
    Frogglet = 0x04,
    Monkeyface = 0x05,
    BiPolarBear = 0x06,
    BiteyBat = 0x07,
    Yeti = 0x08,
    Troll = 0x09,
    Snailburt = 0x0A,
    Giraffey = 0x0B,
    Zebra = 0x0C,
    Meowburt = 0x0D,
    Pazzo = 0x0E,
    BurlyBear = 0x0F,
    Hawkster = 0x10,
    Snoot = 0x11,
    Piggy = 0x12,
    Spiny = 0x13,
    Scratchpaw = 0x14,
    Seahorse = 0x15,
    Chicken = 0x16,
    InstallBall = 0x17,
    MrBuddy = 0x18,
    Sherbert = 0x19,
    Pelter = 0x1A,
    Dragonhead = 0x1B,
    Beholder = 0x1C,
    GoldenWhale = 0x1D
};

} // namespace HookCrashers::Enums

namespace HookCrashers {
using PromptCallback = void(__stdcall*)(void* userData);

// Menu API (see docs/menu-api.md). `value`: 0 for buttons and close, 0/1 for
// toggles, the 0-based option index for choices. Called on the game thread.
using MenuCallback = void(__stdcall*)(const char* menuId, const char* itemId, int value, void* userData);

enum class MenuOpenResult : int {
    Opened = 0,      // shown now
    Queued = 1,      // shown as soon as the main menu is available
    Unavailable = 2, // unsupported build or Menu API disabled (see HookCrashers.log)
    UnknownMenu = 3,
};
}

extern "C" {
HOOKCRASHERS_API const char* __stdcall HookCrashers_GetVersionString();
HOOKCRASHERS_API void __stdcall HookCrashers_LogInfo(const char* message);
HOOKCRASHERS_API void __stdcall HookCrashers_LogWarn(const char* message);
HOOKCRASHERS_API void __stdcall HookCrashers_LogError(const char* message);
HOOKCRASHERS_API bool __stdcall HookCrashers_RegisterCharacter(
    const char* name,
    std::uint8_t startingWeapon,
    std::uint8_t startingPet,
    bool initiallyUnlocked,
    bool freshOnly);
HOOKCRASHERS_API bool __stdcall HookCrashers_CreatePrompt(
    const char* message,
    HookCrashers::PromptCallback onConfirm,
    HookCrashers::PromptCallback onDecline,
    void* userData);

HOOKCRASHERS_API bool __stdcall HookCrashers_MenuCreate(const char* menuId, const char* title);
HOOKCRASHERS_API bool __stdcall HookCrashers_MenuAddButton(
    const char* menuId, const char* itemId, const char* label,
    HookCrashers::MenuCallback onClick, void* userData);
HOOKCRASHERS_API bool __stdcall HookCrashers_MenuAddToggle(
    const char* menuId, const char* itemId, const char* label, bool initial,
    HookCrashers::MenuCallback onChange, void* userData);
HOOKCRASHERS_API bool __stdcall HookCrashers_MenuAddChoice(
    const char* menuId, const char* itemId, const char* label,
    const char* const* options, int optionCount, int initialIndex,
    HookCrashers::MenuCallback onChange, void* userData);
HOOKCRASHERS_API bool __stdcall HookCrashers_MenuAddSubmenu(
    const char* menuId, const char* itemId, const char* label, const char* targetMenuId);
HOOKCRASHERS_API bool __stdcall HookCrashers_MenuAddLabel(const char* menuId, const char* itemId, const char* text);
HOOKCRASHERS_API bool __stdcall HookCrashers_MenuAddGraphic(
    const char* menuId, const char* itemId, const char* graphicId, float x, float y);
HOOKCRASHERS_API bool __stdcall HookCrashers_MenuSetOnClose(
    const char* menuId, HookCrashers::MenuCallback onClose, void* userData);
HOOKCRASHERS_API bool __stdcall HookCrashers_MenuSetBackground(const char* menuId, bool enabled);
HOOKCRASHERS_API int __stdcall HookCrashers_MenuOpen(const char* menuId);
HOOKCRASHERS_API bool __stdcall HookCrashers_MenuClose();
HOOKCRASHERS_API bool __stdcall HookCrashers_MenuSetItemText(const char* menuId, const char* itemId, const char* text);
HOOKCRASHERS_API bool __stdcall HookCrashers_MenuSetItemValue(const char* menuId, const char* itemId, int value);
HOOKCRASHERS_API int __stdcall HookCrashers_MenuGetItemValue(const char* menuId, const char* itemId);
HOOKCRASHERS_API bool __stdcall HookCrashers_MenuSetItemEnabled(const char* menuId, const char* itemId, bool enabled);
HOOKCRASHERS_API bool __stdcall HookCrashers_RegisterMenuGraphic(
    const char* graphicId, const char* swfPath, const char* exportName);
}

namespace HookCrashers {

inline const char* GetVersionString() {
    return HookCrashers_GetVersionString();
}

inline void LogInfo(const char* message) {
    HookCrashers_LogInfo(message);
}

inline void LogWarn(const char* message) {
    HookCrashers_LogWarn(message);
}

inline void LogError(const char* message) {
    HookCrashers_LogError(message);
}

inline bool RegisterCharacter(const char* name,
                              std::uint8_t startingWeapon,
                              std::uint8_t startingPet,
                              bool initiallyUnlocked,
                              bool freshOnly) {
    return HookCrashers_RegisterCharacter(
        name, startingWeapon, startingPet, initiallyUnlocked, freshOnly);
}

inline bool RegisterCharacter(const char* name,
                              Enums::Weapon startingWeapon,
                              Enums::Pet startingPet,
                              bool initiallyUnlocked,
                              bool freshOnly) {
    return HookCrashers_RegisterCharacter(
        name,
        static_cast<std::uint8_t>(startingWeapon),
        static_cast<std::uint8_t>(startingPet),
        initiallyUnlocked,
        freshOnly);
}

inline bool CreatePrompt(const char* message,
                         PromptCallback onConfirm,
                         PromptCallback onDecline,
                         void* userData = nullptr) {
    return HookCrashers_CreatePrompt(message, onConfirm, onDecline, userData);
}

// Thin wrapper over the Menu C API. Ids: letters, digits, '_', '.', '-', ':'
// (max 64). Text is UTF-8. Build a menu once (e.g. in your mod init), then
// Open() it from a callback, a prompt or at startup.
class Menu {
public:
    explicit Menu(const char* id) : m_id(id) {}

    bool Create(const char* title) const { return HookCrashers_MenuCreate(m_id, title); }
    bool AddButton(const char* itemId, const char* label, MenuCallback onClick, void* userData = nullptr) const {
        return HookCrashers_MenuAddButton(m_id, itemId, label, onClick, userData);
    }
    bool AddToggle(const char* itemId, const char* label, bool initial, MenuCallback onChange, void* userData = nullptr) const {
        return HookCrashers_MenuAddToggle(m_id, itemId, label, initial, onChange, userData);
    }
    bool AddChoice(const char* itemId, const char* label, const char* const* options, int optionCount,
                   int initialIndex, MenuCallback onChange, void* userData = nullptr) const {
        return HookCrashers_MenuAddChoice(m_id, itemId, label, options, optionCount, initialIndex, onChange, userData);
    }
    bool AddSubmenu(const char* itemId, const char* label, const char* targetMenuId) const {
        return HookCrashers_MenuAddSubmenu(m_id, itemId, label, targetMenuId);
    }
    bool AddLabel(const char* itemId, const char* text) const { return HookCrashers_MenuAddLabel(m_id, itemId, text); }
    bool AddGraphic(const char* itemId, const char* graphicId, float x, float y) const {
        return HookCrashers_MenuAddGraphic(m_id, itemId, graphicId, x, y);
    }
    bool OnClose(MenuCallback onClose, void* userData = nullptr) const {
        return HookCrashers_MenuSetOnClose(m_id, onClose, userData);
    }
    bool SetBackground(bool enabled) const { return HookCrashers_MenuSetBackground(m_id, enabled); }
    MenuOpenResult Open() const { return static_cast<MenuOpenResult>(HookCrashers_MenuOpen(m_id)); }
    bool SetText(const char* itemId, const char* text) const { return HookCrashers_MenuSetItemText(m_id, itemId, text); }
    bool SetValue(const char* itemId, int value) const { return HookCrashers_MenuSetItemValue(m_id, itemId, value); }
    int GetValue(const char* itemId) const { return HookCrashers_MenuGetItemValue(m_id, itemId); }
    bool SetEnabled(const char* itemId, bool enabled) const { return HookCrashers_MenuSetItemEnabled(m_id, itemId, enabled); }
    const char* Id() const { return m_id; }

private:
    const char* m_id;
};

inline bool CloseMenu() {
    return HookCrashers_MenuClose();
}

inline bool RegisterMenuGraphic(const char* graphicId, const char* swfPath, const char* exportName) {
    return HookCrashers_RegisterMenuGraphic(graphicId, swfPath, exportName);
}

} // namespace HookCrashers
