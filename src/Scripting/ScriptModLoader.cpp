#include "ScriptModLoader.h"
#include "../../include/HookCrashers/Public/Enums.h"
#include "../Save/CharacterConfig.h"
#include "../Util/Logger.h"
#include <json.hpp>
#include <Windows.h>
extern "C" {
#include "../../../External/lua54/lua-5.4.7/src/lua.h"
#include "../../../External/lua54/lua-5.4.7/src/lauxlib.h"
#include "../../../External/lua54/lua-5.4.7/src/lualib.h"
}
#include <fstream>

namespace {

void Lua_SetConst(lua_State* L, const char* name, lua_Integer value) {
    lua_pushinteger(L, value);
    lua_setfield(L, -2, name);
}

void Lua_CreateWeaponEnums(lua_State* L) {
    lua_newtable(L);
    Lua_SetConst(L, "SkinnySword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::SkinnySword));
    Lua_SetConst(L, "ThinSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::ThinSword));
    Lua_SetConst(L, "ThickSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::ThickSword));
    Lua_SetConst(L, "PumpkinPeeler", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::PumpkinPeeler));
    Lua_SetConst(L, "GladiatorSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::GladiatorSword));
    Lua_SetConst(L, "ButcherKnife", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::ButcherKnife));
    Lua_SetConst(L, "HalfSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::HalfSword));
    Lua_SetConst(L, "Carrot", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Carrot));
    Lua_SetConst(L, "ThiefSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::ThiefSword));
    Lua_SetConst(L, "GoldSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::GoldSword));
    Lua_SetConst(L, "DualProngSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::DualProngSword));
    Lua_SetConst(L, "Zigzag", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Zigzag));
    Lua_SetConst(L, "PlaydoPastaMaker", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::PlaydoPastaMaker));
    Lua_SetConst(L, "Falchion", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Falchion));
    Lua_SetConst(L, "PointySword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::PointySword));
    Lua_SetConst(L, "ChewedUpSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::ChewedUpSword));
    Lua_SetConst(L, "FencersFoil", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::FencersFoil));
    Lua_SetConst(L, "BarbarianAxe", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::BarbarianAxe));
    Lua_SetConst(L, "Pitchfork", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Pitchfork));
    Lua_SetConst(L, "CurvedSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::CurvedSword));
    Lua_SetConst(L, "KeySword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::KeySword));
    Lua_SetConst(L, "ApplePeeler", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::ApplePeeler));
    Lua_SetConst(L, "RubberHandleSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::RubberHandleSword));
    Lua_SetConst(L, "Mace", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Mace));
    Lua_SetConst(L, "Club", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Club));
    Lua_SetConst(L, "UglyMace", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::UglyMace));
    Lua_SetConst(L, "RefinedMace", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::RefinedMace));
    Lua_SetConst(L, "Fish", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Fish));
    Lua_SetConst(L, "WrappedSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::WrappedSword));
    Lua_SetConst(L, "SkeletorMace", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::SkeletorMace));
    Lua_SetConst(L, "ClunkyMace", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::ClunkyMace));
    Lua_SetConst(L, "SnakeyMace", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::SnakeyMace));
    Lua_SetConst(L, "RatBeatingBat", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::RatBeatingBat));
    Lua_SetConst(L, "BlackMorningStar", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::BlackMorningStar));
    Lua_SetConst(L, "KingsMace", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::KingsMace));
    Lua_SetConst(L, "MeatTenderizer", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::MeatTenderizer));
    Lua_SetConst(L, "Leaf", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Leaf));
    Lua_SetConst(L, "SheathedSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::SheathedSword));
    Lua_SetConst(L, "PracticeFoil", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::PracticeFoil));
    Lua_SetConst(L, "Twig", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Twig));
    Lua_SetConst(L, "LeafyTwig", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::LeafyTwig));
    Lua_SetConst(L, "LightSaber", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::LightSaber));
    Lua_SetConst(L, "Staff", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Staff));
    Lua_SetConst(L, "WoodenSpoon", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::WoodenSpoon));
    Lua_SetConst(L, "BoneLeg", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::BoneLeg));
    Lua_SetConst(L, "AlienGun", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::AlienGun));
    Lua_SetConst(L, "FishingSpear", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::FishingSpear));
    Lua_SetConst(L, "Lance", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Lance));
    Lua_SetConst(L, "Sai", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Sai));
    Lua_SetConst(L, "UnicornHorn", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::UnicornHorn));
    Lua_SetConst(L, "Ribeye", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Ribeye));
    Lua_SetConst(L, "Kielbasa", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Kielbasa));
    Lua_SetConst(L, "Lobster", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Lobster));
    Lua_SetConst(L, "Umbrella", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Umbrella));
    Lua_SetConst(L, "BroadAx", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::BroadAx));
    Lua_SetConst(L, "EvilSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::EvilSword));
    Lua_SetConst(L, "IceSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::IceSword));
    Lua_SetConst(L, "Candlestick", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Candlestick));
    Lua_SetConst(L, "PanicMallet", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::PanicMallet));
    Lua_SetConst(L, "FishingRod", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::FishingRod));
    Lua_SetConst(L, "Wrench", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Wrench));
    Lua_SetConst(L, "NGLollipop", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::NGLollipop));
    Lua_SetConst(L, "GoldSkullMace", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::GoldSkullMace));
    Lua_SetConst(L, "NGGoldSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::NGGoldSword));
    Lua_SetConst(L, "Chainsaw", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Chainsaw));
    Lua_SetConst(L, "BroadSpear", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::BroadSpear));
    Lua_SetConst(L, "Glowstick", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Glowstick));
    Lua_SetConst(L, "ChickenStick", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::ChickenStick));
    Lua_SetConst(L, "DemonSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::DemonSword));
    Lua_SetConst(L, "BroccoliSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::BroccoliSword));
    Lua_SetConst(L, "ManCatcher", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::ManCatcher));
    Lua_SetConst(L, "WoodenMace", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::WoodenMace));
    Lua_SetConst(L, "NinjaClaw", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::NinjaClaw));
    Lua_SetConst(L, "BuffaloMace", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::BuffaloMace));
    Lua_SetConst(L, "ElectricEel", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::ElectricEel));
    Lua_SetConst(L, "Scissors", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Scissors));
    Lua_SetConst(L, "DinnerFork", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::DinnerFork));
    Lua_SetConst(L, "CattleProd", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::CattleProd));
    Lua_SetConst(L, "LightningBolt", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::LightningBolt));
    Lua_SetConst(L, "TwoByFour", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::TwoByFour));
    Lua_SetConst(L, "WoodenSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::WoodenSword));
    Lua_SetConst(L, "CardboardTube", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::CardboardTube));
    Lua_SetConst(L, "EmeraldSword", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::EmeraldSword));
    Lua_SetConst(L, "Hammer", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Hammer));
    Lua_SetConst(L, "Pencil", static_cast<lua_Integer>(HookCrashers::Enums::Weapon::Pencil));
}

void Lua_CreatePetEnums(lua_State* L) {
    lua_newtable(L);
    Lua_SetConst(L, "None", static_cast<lua_Integer>(HookCrashers::Enums::Pet::None));
    Lua_SetConst(L, "Cardinal", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Cardinal));
    Lua_SetConst(L, "Owlet", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Owlet));
    Lua_SetConst(L, "Rammy", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Rammy));
    Lua_SetConst(L, "Frogglet", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Frogglet));
    Lua_SetConst(L, "Monkeyface", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Monkeyface));
    Lua_SetConst(L, "BiPolarBear", static_cast<lua_Integer>(HookCrashers::Enums::Pet::BiPolarBear));
    Lua_SetConst(L, "BiteyBat", static_cast<lua_Integer>(HookCrashers::Enums::Pet::BiteyBat));
    Lua_SetConst(L, "Yeti", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Yeti));
    Lua_SetConst(L, "Troll", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Troll));
    Lua_SetConst(L, "Snailburt", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Snailburt));
    Lua_SetConst(L, "Giraffey", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Giraffey));
    Lua_SetConst(L, "Zebra", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Zebra));
    Lua_SetConst(L, "Meowburt", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Meowburt));
    Lua_SetConst(L, "Pazzo", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Pazzo));
    Lua_SetConst(L, "BurlyBear", static_cast<lua_Integer>(HookCrashers::Enums::Pet::BurlyBear));
    Lua_SetConst(L, "Hawkster", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Hawkster));
    Lua_SetConst(L, "Snoot", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Snoot));
    Lua_SetConst(L, "Piggy", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Piggy));
    Lua_SetConst(L, "Spiny", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Spiny));
    Lua_SetConst(L, "Scratchpaw", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Scratchpaw));
    Lua_SetConst(L, "Seahorse", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Seahorse));
    Lua_SetConst(L, "Chicken", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Chicken));
    Lua_SetConst(L, "InstallBall", static_cast<lua_Integer>(HookCrashers::Enums::Pet::InstallBall));
    Lua_SetConst(L, "MrBuddy", static_cast<lua_Integer>(HookCrashers::Enums::Pet::MrBuddy));
    Lua_SetConst(L, "Sherbert", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Sherbert));
    Lua_SetConst(L, "Pelter", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Pelter));
    Lua_SetConst(L, "Dragonhead", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Dragonhead));
    Lua_SetConst(L, "Beholder", static_cast<lua_Integer>(HookCrashers::Enums::Pet::Beholder));
    Lua_SetConst(L, "GoldenWhale", static_cast<lua_Integer>(HookCrashers::Enums::Pet::GoldenWhale));
}

std::string GetHookCrashersModsDirectoryA() {
    char modulePath[MAX_PATH] = {};
    HMODULE self = GetModuleHandleA("HookCrashers.asi");
    if (!self || !GetModuleFileNameA(self, modulePath, MAX_PATH)) {
        return "mods";
    }

    char* lastSlash = strrchr(modulePath, '\\');
    if (!lastSlash) {
        return "mods";
    }
    *(lastSlash + 1) = '\0';
    return std::string(modulePath) + "mods";
}

int Lua_RegisterCharacter(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    const int weapon = static_cast<int>(luaL_checkinteger(L, 2));
    const int pet = static_cast<int>(luaL_checkinteger(L, 3));
    const bool unlocked = lua_toboolean(L, 4) != 0;
    const bool freshOnly = lua_toboolean(L, 5) != 0;

    HookCrashers::Config::AddonCharacterDef def;
    def.id = id;
    def.weapon = static_cast<uint8_t>(weapon);
    def.animal = static_cast<uint8_t>(pet);
    def.unlocked = unlocked;
    def.freshOnly = freshOnly;
    HookCrashers::Save::CharacterConfig::Instance().RegisterCharacter(def);
    return 0;
}

int Lua_LogInfo(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    HookCrashers::Util::Logger::Instance().Get()->info("[Lua] {}", message);
    return 0;
}

int Lua_LogWarn(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    HookCrashers::Util::Logger::Instance().Get()->warn("[Lua] {}", message);
    return 0;
}

int Lua_LogError(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    HookCrashers::Util::Logger::Instance().Get()->error("[Lua] {}", message);
    return 0;
}

int Lua_OpenHookCrashers(lua_State* L) {
    lua_newtable(L);

    lua_newtable(L);
    lua_pushcfunction(L, Lua_RegisterCharacter);
    lua_setfield(L, -2, "Register");
    lua_setfield(L, -2, "Character");

    lua_getfield(L, -1, "Character");
    lua_getfield(L, -1, "Register");
    lua_setfield(L, -3, "RegisterCharacter");
    lua_pop(L, 1);

    lua_newtable(L);
    lua_pushcfunction(L, Lua_LogInfo);
    lua_setfield(L, -2, "Info");
    lua_pushcfunction(L, Lua_LogWarn);
    lua_setfield(L, -2, "Warn");
    lua_pushcfunction(L, Lua_LogError);
    lua_setfield(L, -2, "Error");
    lua_setfield(L, -2, "Log");

    lua_newtable(L);
    Lua_CreateWeaponEnums(L);
    lua_setfield(L, -2, "Weapon");
    Lua_CreatePetEnums(L);
    lua_setfield(L, -2, "Pet");
    lua_setfield(L, -2, "Enums");

    lua_getfield(L, -1, "Enums");
    lua_setglobal(L, "Enums");
    return 1;
}

} // namespace

namespace HookCrashers {
namespace Scripting {

ScriptModLoader& ScriptModLoader::Instance() {
    static ScriptModLoader instance;
    return instance;
}

bool ScriptModLoader::DiscoverAndLoad() {
    m_loadedMods.clear();
    Save::CharacterConfig::Instance().ClearRegisteredCharacters();
    const std::string modsRoot = GetHookCrashersModsDirectoryA();
    Util::Logger::Instance().Get()->info("[ScriptModLoader] Scanning folder mods in '{}'.", modsRoot);

    WIN32_FIND_DATAA data{};
    HANDLE findHandle = FindFirstFileA((modsRoot + "\\*").c_str(), &data);
    if (findHandle == INVALID_HANDLE_VALUE) {
        CreateDirectoryA(modsRoot.c_str(), NULL);
        Util::Logger::Instance().Get()->info("[ScriptModLoader] Created mods directory at '{}'.", modsRoot);
        return true;
    }

    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0) continue;
        LoadModDirectory(modsRoot, data.cFileName);
    } while (FindNextFileA(findHandle, &data));

    FindClose(findHandle);
    Util::Logger::Instance().Get()->info("[ScriptModLoader] Finished folder mod scan. loaded_mods={}.", m_loadedMods.size());
    return true;
}

bool ScriptModLoader::LoadModDirectory(const std::string& modRoot, const std::string& folderName) {
    HookCrashers::Util::Logger::ScopedLogContext scopedLog(folderName);
    ScriptModInfo info{};
    info.name = folderName;
    info.directory = modRoot + "\\" + folderName + "\\";
    info.mainLuaPath = info.directory + "main.lua";
    info.localizationPath = info.directory + "locs.json";
    info.manifestPath = info.directory + "manifest.json";
    info.iconPath = info.directory + "icon.png";

    info.hasMainLua = GetFileAttributesA(info.mainLuaPath.c_str()) != INVALID_FILE_ATTRIBUTES;
    info.hasLocalizations = GetFileAttributesA(info.localizationPath.c_str()) != INVALID_FILE_ATTRIBUTES;
    info.hasManifest = GetFileAttributesA(info.manifestPath.c_str()) != INVALID_FILE_ATTRIBUTES;
    info.hasIcon = GetFileAttributesA(info.iconPath.c_str()) != INVALID_FILE_ATTRIBUTES;

    if (!info.hasMainLua && !info.hasLocalizations && !info.hasManifest && !info.hasIcon) {
        return false;
    }

    if (info.hasManifest) {
        ParseManifestJson(info.manifestPath, info);
    }

    Util::Logger::Instance().Get()->info(
        "[ScriptModLoader] Mod folder '{}' detected. manifest={} main.lua={} locs.json={} icon.png={}.",
        folderName,
        info.hasManifest,
        info.hasMainLua,
        info.hasLocalizations,
        info.hasIcon);

    if (info.hasMainLua) {
        ParseMainLua(info.mainLuaPath, info);
    }

    if (info.hasLocalizations) {
        ParseLocsJson(info.localizationPath, info);
    }

    m_loadedMods.push_back(info);
    Util::Logger::Instance().Get()->info(
        "[ScriptModLoader] Loaded folder mod '{}' author='{}' version='{}' characters={} localizations={}.",
        info.name,
        info.author,
        info.version,
        info.registeredCharacterCount,
        info.registeredLocalizationCount);
    return true;
}

bool ScriptModLoader::ParseManifestJson(const std::string& path, ScriptModInfo& modInfo) {
    HookCrashers::Util::Logger::ScopedLogContext scopedLog(modInfo.name);
    std::ifstream file(path);
    if (!file.is_open()) {
        Util::Logger::Instance().Get()->error("[ScriptModLoader] Failed to open manifest '{}'.", path);
        return false;
    }

    try {
        nlohmann::json root;
        file >> root;
        modInfo.name = root.value("name", modInfo.name);
        modInfo.author = root.value("author", modInfo.author);
        modInfo.version = root.value("version", modInfo.version);
        Util::Logger::Instance().Get()->info(
            "[ScriptModLoader] Parsed manifest '{}' -> name='{}' author='{}' version='{}'.",
            path,
            modInfo.name,
            modInfo.author,
            modInfo.version);
        return true;
    }
    catch (const std::exception& e) {
        Util::Logger::Instance().Get()->error("[ScriptModLoader] Failed to parse manifest '{}': {}", path, e.what());
        return false;
    }
}

bool ScriptModLoader::ParseMainLua(const std::string& path, ScriptModInfo& modInfo) {
    HookCrashers::Util::Logger::ScopedLogContext scopedLog(modInfo.name);
    const int beforeCount = Save::CharacterConfig::Instance().GetAddonCount();

    lua_State* L = luaL_newstate();
    if (!L) {
        Util::Logger::Instance().Get()->error("[ScriptModLoader] Failed to create lua_State for '{}'.", path);
        return false;
    }
    luaL_openlibs(L);
    luaL_requiref(L, "HookCrashers", Lua_OpenHookCrashers, 1);
    lua_pop(L, 1);

    Util::Logger::Instance().Get()->info("[ScriptModLoader] Executing main.lua for mod '{}' from '{}'.", modInfo.name, path);
    if (luaL_dofile(L, path.c_str()) != LUA_OK) {
        Util::Logger::Instance().Get()->error("[ScriptModLoader] Lua error in '{}': {}", path, lua_tostring(L, -1));
        lua_close(L);
        return false;
    }

    const int afterCount = Save::CharacterConfig::Instance().GetAddonCount();
    const auto& addons = Save::CharacterConfig::Instance().GetAddons();
    modInfo.registeredCharacterCount = static_cast<size_t>(afterCount - beforeCount);
    for (int i = beforeCount; i < afterCount; ++i) {
        modInfo.registeredCharacterIds.push_back(addons[static_cast<size_t>(i)].id);
    }

    if (modInfo.registeredCharacterIds.empty()) {
        Util::Logger::Instance().Get()->info("[ScriptModLoader] Mod '{}' did not register any addon characters.", modInfo.name);
    }
    else {
        for (const auto& id : modInfo.registeredCharacterIds) {
            Util::Logger::Instance().Get()->info("[ScriptModLoader] Mod '{}' registered character '{}'.", modInfo.name, id);
        }
    }

    lua_close(L);
    return true;
}

bool ScriptModLoader::ParseLocsJson(const std::string& path, ScriptModInfo& modInfo) {
    HookCrashers::Util::Logger::ScopedLogContext scopedLog(modInfo.name);
    std::ifstream file(path);
    if (!file.is_open()) {
        Util::Logger::Instance().Get()->error("[ScriptModLoader] Failed to open '{}'.", path);
        return false;
    }

    try {
        nlohmann::json root;
        file >> root;
        modInfo.registeredLocalizationIds.clear();
        if (root.contains("strings") && root["strings"].is_array()) {
            for (const auto& item : root["strings"]) {
                if (item.contains("unlocalized_name")) {
                    modInfo.registeredLocalizationIds.push_back(item.at("unlocalized_name").get<std::string>());
                }
                else if (item.contains("id")) {
                    modInfo.registeredLocalizationIds.push_back(item.at("id").get<std::string>());
                }
            }
        }

        modInfo.registeredLocalizationCount = modInfo.registeredLocalizationIds.size();
        if (modInfo.registeredLocalizationIds.empty()) {
            Util::Logger::Instance().Get()->warn("[ScriptModLoader] Mod '{}' has a locs.json file but no string entries.", modInfo.name);
        }
        else {
            for (const auto& id : modInfo.registeredLocalizationIds) {
                Util::Logger::Instance().Get()->info("[ScriptModLoader] Mod '{}' declares localization '{}'.", modInfo.name, id);
            }
        }
    }
    catch (const std::exception& e) {
        Util::Logger::Instance().Get()->error("[ScriptModLoader] Failed to parse '{}': {}", path, e.what());
        return false;
    }
    return true;
}

} // namespace Scripting
} // namespace HookCrashers
