#include "LuaApi.h"

#include "Natives.h"
#include "../Characters/CharacterRegistry.h"
#include "../Characters/CharacterExpansion.h"
#include "../Core/Logger.h"
#include "../Localization/LocalizationSystem.h"
#include "../UI/UpdatePromptMenu.h"

#include <windows.h>

#if defined(HOOKCRASHERS_ENABLE_LUA_API)
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <cctype>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace HookCrashers::NativeDB::Lua {
namespace {

thread_local std::string g_currentModDirectory;
thread_local std::string g_currentModVersion;


std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (a.back() == '\\' || a.back() == '/') return a + b;
    return a + "\\" + b;
}

std::string ReadTextFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

void SkipJsonWhitespace(const std::string& text, std::size_t& cursor) {
    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
        ++cursor;
    }
}

bool ParseJsonString(const std::string& text, std::size_t& cursor, std::string& out) {
    SkipJsonWhitespace(text, cursor);
    if (cursor >= text.size() || text[cursor] != '"') return false;
    ++cursor;
    out.clear();
    while (cursor < text.size()) {
        const char ch = text[cursor++];
        if (ch == '"') return true;
        if (ch != '\\') {
            out.push_back(ch);
            continue;
        }
        if (cursor >= text.size()) return false;
        const char escape = text[cursor++];
        switch (escape) {
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        default: out.push_back(escape); break;
        }
    }
    return false;
}

bool ConsumeJsonChar(const std::string& text, std::size_t& cursor, char expected) {
    SkipJsonWhitespace(text, cursor);
    if (cursor >= text.size() || text[cursor] != expected) return false;
    ++cursor;
    return true;
}

std::unordered_map<std::string, std::string> ParseLocalizationJson(const std::string& text) {
    std::unordered_map<std::string, std::string> values;
    std::size_t cursor = 0;
    if (!ConsumeJsonChar(text, cursor, '{')) return values;

    while (cursor < text.size()) {
        SkipJsonWhitespace(text, cursor);
        if (cursor < text.size() && text[cursor] == '}') break;

        std::string language;
        if (!ParseJsonString(text, cursor, language) ||
            !ConsumeJsonChar(text, cursor, ':') ||
            !ConsumeJsonChar(text, cursor, '{')) {
            break;
        }

        while (cursor < text.size()) {
            SkipJsonWhitespace(text, cursor);
            if (cursor < text.size() && text[cursor] == '}') {
                ++cursor;
                break;
            }

            std::string key;
            std::string value;
            if (!ParseJsonString(text, cursor, key) ||
                !ConsumeJsonChar(text, cursor, ':') ||
                !ParseJsonString(text, cursor, value)) {
                return values;
            }
            values[language + "\x1F" + key] = value;

            SkipJsonWhitespace(text, cursor);
            if (cursor < text.size() && text[cursor] == ',') ++cursor;
        }

        SkipJsonWhitespace(text, cursor);
        if (cursor < text.size() && text[cursor] == ',') ++cursor;
    }
    return values;
}

std::string CurrentLocalizationLanguage() {
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

bool ResolveCurrentModLocalization(const std::string& id, std::string& out) {
    return HookCrashers::Localization::Resolve(id, out);
}

Natives ReadNative(lua_State* L, int index) {
    if (lua_isstring(L, index)) {
        const char* name = lua_tostring(L, index);
        const NativeInfo* info = FindNativeInfo(name);
        if (!info) {
            luaL_error(L, "unknown native '%s'", name ? name : "<null>");
        }
        return info->id;
    }
    return static_cast<Natives>(
        static_cast<std::uintptr_t>(luaL_checkinteger(L, index)));
}

std::uintptr_t ReadArg(lua_State* L, int index) {
    if (lua_isnoneornil(L, index)) return 0;
    if (lua_islightuserdata(L, index)) {
        return reinterpret_cast<std::uintptr_t>(lua_touserdata(L, index));
    }
    return static_cast<std::uintptr_t>(luaL_checkinteger(L, index));
}

int Lua_SetGameBase(lua_State* L) {
    SetGameBase(static_cast<std::uintptr_t>(luaL_checkinteger(L, 1)));
    return 0;
}

int Lua_GetGameBase(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(GetGameBase()));
    return 1;
}

int Lua_NativeAddress(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(NativeAddress(ReadNative(L, 1))));
    return 1;
}

int Lua_CallNative(lua_State* L) {
    const int count = lua_gettop(L);
    const Natives native = ReadNative(L, 1);
    const std::uintptr_t result = CallNativeRaw(
        native,
        count >= 2 ? ReadArg(L, 2) : 0,
        count >= 3 ? ReadArg(L, 3) : 0,
        count >= 4 ? ReadArg(L, 4) : 0,
        count >= 5 ? ReadArg(L, 5) : 0,
        count >= 6 ? ReadArg(L, 6) : 0,
        count >= 7 ? ReadArg(L, 7) : 0);
    lua_pushinteger(L, static_cast<lua_Integer>(result));
    return 1;
}

int Lua_RegisterCharacter(lua_State* L) {
    HookCrashers::Characters::AddonCharacter character;
    character.name = luaL_checkstring(L, 1);
    character.assetDirectory = g_currentModDirectory + "\\characters\\" + character.name;
    character.startingWeapon = static_cast<std::uint8_t>(luaL_checkinteger(L, 2));
    character.startingPet = static_cast<std::uint8_t>(luaL_checkinteger(L, 3));
    character.initiallyUnlocked = lua_toboolean(L, 4) != 0;
    character.freshOnly = lua_toboolean(L, 5) != 0;
    lua_pushboolean(L, HookCrashers::Characters::Registry::Instance().RegisterCharacter(character));
    return 1;
}

int Lua_InstallCharacterExpansion(lua_State* L) {
    lua_pushboolean(L, HookCrashers::Characters::CharacterExpansion::Install());
    return 1;
}

struct LuaPromptCallbacks {
    lua_State* state = nullptr;
    int confirm = LUA_NOREF;
    int decline = LUA_NOREF;

    ~LuaPromptCallbacks() {
        if (!state) return;
        if (confirm != LUA_NOREF) luaL_unref(state, LUA_REGISTRYINDEX, confirm);
        if (decline != LUA_NOREF) luaL_unref(state, LUA_REGISTRYINDEX, decline);
    }

    void Invoke(int reference) {
        if (reference == LUA_NOREF) return;
        lua_rawgeti(state, LUA_REGISTRYINDEX, reference);
        if (lua_pcall(state, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(state, -1);
            HookCrashers::Core::Logger::Error(
                "Lua CreatePrompt callback failed: %s", error ? error : "<unknown>");
            lua_pop(state, 1);
        }
    }
};

int ReadOptionalFunction(lua_State* L, int index) {
    if (lua_isnoneornil(L, index)) return LUA_NOREF;
    luaL_checktype(L, index, LUA_TFUNCTION);
    lua_pushvalue(L, index);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

int Lua_CreatePrompt(lua_State* L) {
    const char* message = luaL_checkstring(L, 1);
    auto callbacks = std::make_shared<LuaPromptCallbacks>();
    callbacks->state = L;
    callbacks->confirm = ReadOptionalFunction(L, 2);
    callbacks->decline = ReadOptionalFunction(L, 3);

    const bool created = HookCrashers::UI::UpdatePromptMenu::CreatePrompt(
        message ? message : "",
        [callbacks]() { callbacks->Invoke(callbacks->confirm); },
        [callbacks]() { callbacks->Invoke(callbacks->decline); });
    lua_pushboolean(L, created);
    return 1;
}


int Lua_SetVersionText(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    std::string text;
    if (!id || !ResolveCurrentModLocalization(id, text)) {
        HookCrashers::Core::Logger::Warn("SetVersionText could not resolve localization id '%s'", id ? id : "<null>");
        lua_pushboolean(L, 0);
        return 1;
    }

    HookCrashers::UI::UpdatePromptMenu::SetVersionTextTemplate(text, g_currentModVersion);
    lua_pushboolean(L, 1);
    return 1;
}

int Lua_OpenHookCrashers(lua_State* L) {
    lua_getglobal(L, "HookCrashers");
    return 1;
}

void RegisterNativeTable(lua_State* L) {
    lua_newtable(L);
    for (const NativeInfo& info : kNativeInfos) {
        lua_pushinteger(L, static_cast<lua_Integer>(info.rva));
        lua_setfield(L, -2, info.name);
    }
    lua_setglobal(L, "Natives");
}

void SetFunction(lua_State* L, const char* name, lua_CFunction function) {
    lua_pushcfunction(L, function);
    lua_setglobal(L, name);
}

void RegisterHookCrashersModule(lua_State* L) {
    lua_newtable(L);

    lua_newtable(L);
    lua_pushcfunction(L, Lua_RegisterCharacter);
    lua_setfield(L, -2, "Register");
    lua_setfield(L, -2, "Character");

    lua_pushcfunction(L, Lua_RegisterCharacter);
    lua_setfield(L, -2, "RegisterCharacter");
    lua_pushcfunction(L, Lua_CreatePrompt);
    lua_setfield(L, -2, "CreatePrompt");
    lua_pushcfunction(L, Lua_SetVersionText);
    lua_setfield(L, -2, "SetVersionText");

    lua_newtable(L);
    lua_pushcfunction(L, Lua_CreatePrompt);
    lua_setfield(L, -2, "CreatePrompt");
    lua_pushcfunction(L, Lua_SetVersionText);
    lua_setfield(L, -2, "SetVersionText");
    lua_setfield(L, -2, "UI");

    lua_newtable(L);
    lua_setfield(L, -2, "Enums");

    lua_pushvalue(L, -1);
    lua_setglobal(L, "HookCrashers");
    lua_pushvalue(L, -1);
    lua_setglobal(L, "HC");
    lua_pop(L, 1);

    lua_getglobal(L, "HookCrashers");
    lua_getfield(L, -1, "Enums");
    lua_setglobal(L, "Enums");
    lua_pop(L, 1);

    luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
    lua_pushcfunction(L, Lua_OpenHookCrashers);
    lua_setfield(L, -2, "HookCrashers");
    lua_pop(L, 1);
}

} // namespace

void SetCurrentModDirectory(const std::string& directory) {
    g_currentModDirectory = directory;
}

void SetCurrentModContext(const std::string& directory, const std::string& version) {
    g_currentModDirectory = directory;
    g_currentModVersion = version;
}

bool Register(lua_State* L) {
    if (!L) return false;
    RegisterNativeTable(L);
    SetFunction(L, "SetGameBase", Lua_SetGameBase);
    SetFunction(L, "GetGameBase", Lua_GetGameBase);
    SetFunction(L, "NativeAddress", Lua_NativeAddress);
    SetFunction(L, "CallNative", Lua_CallNative);
    SetFunction(L, "RegisterCharacter", Lua_RegisterCharacter);
    SetFunction(L, "InstallCharacterExpansion", Lua_InstallCharacterExpansion);
    SetFunction(L, "CreatePrompt", Lua_CreatePrompt);
    SetFunction(L, "SetVersionText", Lua_SetVersionText);
    RegisterHookCrashersModule(L);
    return true;
}

} // namespace HookCrashers::NativeDB::Lua
#else
namespace HookCrashers::NativeDB::Lua {

void SetCurrentModDirectory(const std::string&) {}
void SetCurrentModContext(const std::string&, const std::string&) {}

bool Register(lua_State*) {
    return false;
}

} // namespace HookCrashers::NativeDB::Lua
#endif
