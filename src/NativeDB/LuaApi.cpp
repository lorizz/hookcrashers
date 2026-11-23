#include "LuaApi.h"

#include "Natives.h"
#include "../Characters/CharacterRegistry.h"
#include "../Characters/CharacterExpansion.h"
#include "../Core/Logger.h"
#include "../Localization/LocalizationSystem.h"
#include "../UI/MenuApi.h"
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

// --- HC.Menu -------------------------------------------------------------------
namespace MenuApi = HookCrashers::UI::Menus::Api;
using HookCrashers::UI::Menus::ItemKind;
constexpr const char* kMenuMetatable = "HookCrashers.Menu";

struct LuaFunctionRef {
    lua_State* state = nullptr;
    int reference = LUA_NOREF;
};

LuaFunctionRef KeepFunction(lua_State* L, int index) {
    LuaFunctionRef ref;
    if (lua_isnoneornil(L, index)) return ref;
    luaL_checktype(L, index, LUA_TFUNCTION);
    lua_pushvalue(L, index);
    ref.state = L;
    ref.reference = luaL_ref(L, LUA_REGISTRYINDEX); // menus live for the whole session
    return ref;
}

template <typename PushArgs>
void CallLua(const LuaFunctionRef& ref, const char* what, PushArgs pushArgs) {
    if (!ref.state || ref.reference == LUA_NOREF) return;
    lua_State* L = ref.state;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref.reference);
    const int count = pushArgs(L);
    if (lua_pcall(L, count, 0, 0) != LUA_OK) {
        const char* error = lua_tostring(L, -1);
        HookCrashers::Core::Logger::Error("Lua menu %s callback failed: %s", what, error ? error : "<unknown>");
        lua_pop(L, 1);
    }
}

std::string MenuIdArg(lua_State* L, int index) {
    if (lua_istable(L, index)) {
        lua_getfield(L, index, "id");
        const char* id = lua_tostring(L, -1);
        std::string value = id ? id : "";
        lua_pop(L, 1);
        if (value.empty()) luaL_argerror(L, index, "menu object without id");
        return value;
    }
    return luaL_checkstring(L, index);
}

void PushMenuObject(lua_State* L, const std::string& menuId) {
    lua_newtable(L);
    lua_pushstring(L, menuId.c_str());
    lua_setfield(L, -2, "id");
    luaL_setmetatable(L, kMenuMetatable);
}

bool FindItemKind(const std::string& menuId, const std::string& itemId, ItemKind& kind, std::vector<std::string>& options) {
    HookCrashers::UI::Menus::Menu menu;
    if (!HookCrashers::UI::Menus::Registry::Instance().Snapshot(menuId, menu)) return false;
    for (const auto& item : menu.items) {
        if (item.id == itemId) {
            kind = item.kind;
            options = item.options;
            return true;
        }
    }
    return false;
}

std::vector<std::string> StringList(lua_State* L, int index) {
    luaL_checktype(L, index, LUA_TTABLE);
    std::vector<std::string> values;
    const lua_Integer count = luaL_len(L, index);
    for (lua_Integer i = 1; i <= count; ++i) {
        lua_rawgeti(L, index, i);
        const char* value = lua_tostring(L, -1);
        if (!value) luaL_argerror(L, index, "options must be strings");
        values.emplace_back(value);
        lua_pop(L, 1);
    }
    return values;
}

int Lua_MenuCreate(lua_State* L) {
    const std::string id = luaL_checkstring(L, 1);
    const char* title = luaL_optstring(L, 2, "");
    if (!MenuApi::CreateMenu(id, title)) {
        lua_pushnil(L);
        return 1;
    }
    PushMenuObject(L, id);
    return 1;
}

int Lua_MenuGet(lua_State* L) {
    const std::string id = luaL_checkstring(L, 1);
    if (!HookCrashers::UI::Menus::Registry::Instance().Exists(id)) {
        lua_pushnil(L);
        return 1;
    }
    PushMenuObject(L, id);
    return 1;
}

int Lua_MenuAddButton(lua_State* L) {
    const std::string menu = MenuIdArg(L, 1);
    const std::string item = luaL_checkstring(L, 2);
    const char* label = luaL_checkstring(L, 3);
    const LuaFunctionRef ref = KeepFunction(L, 4);
    lua_pushboolean(L, MenuApi::AddButton(menu, item, label, [ref](int) {
        CallLua(ref, "button", [](lua_State*) { return 0; });
    }));
    return 1;
}

int Lua_MenuAddToggle(lua_State* L) {
    const std::string menu = MenuIdArg(L, 1);
    const std::string item = luaL_checkstring(L, 2);
    const char* label = luaL_checkstring(L, 3);
    const bool initial = lua_toboolean(L, 4) != 0;
    const LuaFunctionRef ref = KeepFunction(L, 5);
    std::vector<std::string> labels;
    if (!lua_isnoneornil(L, 6) || !lua_isnoneornil(L, 7)) {
        labels = {luaL_checkstring(L, 6), luaL_checkstring(L, 7)};
    }
    lua_pushboolean(L, MenuApi::AddToggle(menu, item, label, initial, [ref](int value) {
        CallLua(ref, "toggle", [value](lua_State* S) { lua_pushboolean(S, value != 0); return 1; });
    }, std::move(labels)));
    return 1;
}

int Lua_MenuAddChoice(lua_State* L) {
    const std::string menu = MenuIdArg(L, 1);
    const std::string item = luaL_checkstring(L, 2);
    const char* label = luaL_checkstring(L, 3);
    std::vector<std::string> options = StringList(L, 4);
    const int initial = static_cast<int>(luaL_optinteger(L, 5, 1)) - 1;
    const LuaFunctionRef ref = KeepFunction(L, 6);
    const std::vector<std::string> names = options;
    lua_pushboolean(L, MenuApi::AddChoice(menu, item, label, std::move(options), initial, [ref, names](int value) {
        CallLua(ref, "choice", [value, &names](lua_State* S) {
            lua_pushinteger(S, value + 1);
            lua_pushstring(S, value >= 0 && value < static_cast<int>(names.size()) ? names[value].c_str() : "");
            return 2;
        });
    }));
    return 1;
}

int Lua_MenuAddSubmenu(lua_State* L) {
    const std::string menu = MenuIdArg(L, 1);
    lua_pushboolean(L, MenuApi::AddSubmenu(menu, luaL_checkstring(L, 2), luaL_checkstring(L, 3), MenuIdArg(L, 4)));
    return 1;
}

int Lua_MenuAddLabel(lua_State* L) {
    const std::string menu = MenuIdArg(L, 1);
    lua_pushboolean(L, MenuApi::AddLabel(menu, luaL_checkstring(L, 2), luaL_checkstring(L, 3)));
    return 1;
}

int Lua_MenuAddGraphic(lua_State* L) {
    const std::string menu = MenuIdArg(L, 1);
    lua_pushboolean(L, MenuApi::AddGraphic(menu, luaL_checkstring(L, 2), luaL_checkstring(L, 3),
                                           static_cast<float>(luaL_checknumber(L, 4)),
                                           static_cast<float>(luaL_checknumber(L, 5))));
    return 1;
}

int Lua_MenuOnClose(lua_State* L) {
    const std::string menu = MenuIdArg(L, 1);
    const LuaFunctionRef ref = KeepFunction(L, 2);
    HookCrashers::UI::Menus::CloseCallback callback;
    if (ref.reference != LUA_NOREF) {
        callback = [ref]() { CallLua(ref, "close", [](lua_State*) { return 0; }); };
    }
    lua_pushboolean(L, MenuApi::SetOnClose(menu, std::move(callback)));
    return 1;
}

int Lua_MenuSetBackground(lua_State* L) {
    const std::string menu = MenuIdArg(L, 1);
    lua_pushboolean(L, MenuApi::SetBackground(menu, lua_toboolean(L, 2) != 0));
    return 1;
}

int Lua_MenuOpen(lua_State* L) {
    const auto result = MenuApi::Open(MenuIdArg(L, 1));
    using HookCrashers::UI::Menus::Native::OpenResult;
    lua_pushboolean(L, result == OpenResult::Opened || result == OpenResult::Queued);
    lua_pushstring(L, MenuApi::OpenResultName(result));
    return 2;
}

int Lua_MenuClose(lua_State* L) {
    lua_pushboolean(L, MenuApi::Close());
    return 1;
}

int Lua_MenuCurrent(lua_State* L) {
    const std::string id = MenuApi::CurrentMenuId();
    if (id.empty()) lua_pushnil(L);
    else lua_pushstring(L, id.c_str());
    return 1;
}

int Lua_MenuSetText(lua_State* L) {
    const std::string menu = MenuIdArg(L, 1);
    lua_pushboolean(L, MenuApi::SetItemText(menu, luaL_checkstring(L, 2), luaL_checkstring(L, 3)));
    return 1;
}

int Lua_MenuSetValue(lua_State* L) {
    const std::string menu = MenuIdArg(L, 1);
    const std::string item = luaL_checkstring(L, 2);
    ItemKind kind {};
    std::vector<std::string> options;
    if (!FindItemKind(menu, item, kind, options)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int value = kind == ItemKind::Toggle ? (lua_toboolean(L, 3) ? 1 : 0)
                                               : static_cast<int>(luaL_checkinteger(L, 3)) - 1;
    lua_pushboolean(L, MenuApi::SetItemValue(menu, item, value));
    return 1;
}

int Lua_MenuGetValue(lua_State* L) {
    const std::string menu = MenuIdArg(L, 1);
    const std::string item = luaL_checkstring(L, 2);
    ItemKind kind {};
    std::vector<std::string> options;
    int value = 0;
    if (!FindItemKind(menu, item, kind, options) || !MenuApi::GetItemValue(menu, item, value)) {
        lua_pushnil(L);
        return 1;
    }
    if (kind == ItemKind::Toggle) {
        lua_pushboolean(L, value != 0);
        return 1;
    }
    lua_pushinteger(L, value + 1);
    lua_pushstring(L, value >= 0 && value < static_cast<int>(options.size()) ? options[value].c_str() : "");
    return 2;
}

int Lua_MenuSetEnabled(lua_State* L) {
    const std::string menu = MenuIdArg(L, 1);
    lua_pushboolean(L, MenuApi::SetItemEnabled(menu, luaL_checkstring(L, 2), lua_toboolean(L, 3) != 0));
    return 1;
}

int Lua_MenuRegisterGraphic(lua_State* L) {
    const std::string id = luaL_checkstring(L, 1);
    std::string path = luaL_checkstring(L, 2);
    const std::string exportName = luaL_checkstring(L, 3);
    const bool absolute = path.size() > 2 && (path[1] == ':' || path[0] == '\\' || path[0] == '/');
    if (!absolute) path = JoinPath(g_currentModDirectory, path);
    lua_pushboolean(L, MenuApi::RegisterGraphic(id, path, exportName));
    return 1;
}

void RegisterMenuModule(lua_State* L) {
    // Methods shared by HC.Menu.* (menu id or object first) and menu objects.
    const luaL_Reg methods[] = {
        {"AddButton", Lua_MenuAddButton},
        {"AddToggle", Lua_MenuAddToggle},
        {"AddChoice", Lua_MenuAddChoice},
        {"AddSubmenu", Lua_MenuAddSubmenu},
        {"AddLabel", Lua_MenuAddLabel},
        {"AddGraphic", Lua_MenuAddGraphic},
        {"OnClose", Lua_MenuOnClose},
        {"SetBackground", Lua_MenuSetBackground},
        {"Open", Lua_MenuOpen},
        {"SetText", Lua_MenuSetText},
        {"SetValue", Lua_MenuSetValue},
        {"GetValue", Lua_MenuGetValue},
        {"SetEnabled", Lua_MenuSetEnabled},
        {nullptr, nullptr},
    };
    luaL_newmetatable(L, kMenuMetatable);
    lua_newtable(L);
    luaL_setfuncs(L, methods, 0);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

    lua_newtable(L);
    luaL_setfuncs(L, methods, 0);
    const luaL_Reg module[] = {
        {"Create", Lua_MenuCreate},
        {"Get", Lua_MenuGet},
        {"Close", Lua_MenuClose},
        {"Current", Lua_MenuCurrent},
        {"RegisterGraphic", Lua_MenuRegisterGraphic},
        {nullptr, nullptr},
    };
    luaL_setfuncs(L, module, 0);
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

    RegisterMenuModule(L);
    lua_setfield(L, -2, "Menu");

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
