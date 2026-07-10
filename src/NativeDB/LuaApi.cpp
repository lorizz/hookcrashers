#include "LuaApi.h"

#include "Natives.h"
#include "../Characters/CharacterRegistry.h"
#include "../Characters/CharacterExpansion.h"

#if defined(HOOKCRASHERS_ENABLE_LUA_API)
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace HookCrashers::NativeDB::Lua {
namespace {

Natives ReadNative(lua_State* L, int index) {
    if (lua_isstring(L, index)) {
        const char* name = lua_tostring(L, index);
        const NativeInfo* info = FindNativeInfo(name);
        if (!info) {
            luaL_error(L, "unknown native '%s'", name ? name : "<null>");
        }
        return info->id;
    }

    const std::uintptr_t rva = static_cast<std::uintptr_t>(luaL_checkinteger(L, index));
    return static_cast<Natives>(rva);
}

std::uintptr_t ReadArg(lua_State* L, int index) {
    if (lua_isnoneornil(L, index)) {
        return 0;
    }
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
    const Natives native = ReadNative(L, 1);
    lua_pushinteger(L, static_cast<lua_Integer>(NativeAddress(native)));
    return 1;
}

int Lua_CallNative(lua_State* L) {
    const int count = lua_gettop(L);
    const Natives native = ReadNative(L, 1);
    const std::uintptr_t a0 = count >= 2 ? ReadArg(L, 2) : 0;
    const std::uintptr_t a1 = count >= 3 ? ReadArg(L, 3) : 0;
    const std::uintptr_t a2 = count >= 4 ? ReadArg(L, 4) : 0;
    const std::uintptr_t a3 = count >= 5 ? ReadArg(L, 5) : 0;
    const std::uintptr_t a4 = count >= 6 ? ReadArg(L, 6) : 0;
    const std::uintptr_t a5 = count >= 7 ? ReadArg(L, 7) : 0;
    const std::uintptr_t result = CallNativeRaw(native, a0, a1, a2, a3, a4, a5);
    lua_pushinteger(L, static_cast<lua_Integer>(result));
    return 1;
}

int Lua_RegisterCharacter(lua_State* L) {
    HookCrashers::Characters::AddonCharacter character;
    character.name = luaL_checkstring(L, 1);
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

void RegisterNativeTable(lua_State* L) {
    lua_newtable(L);
    for (const NativeInfo& info : kNativeInfos) {
        lua_pushinteger(L, static_cast<lua_Integer>(info.rva));
        lua_setfield(L, -2, info.name);
    }
    lua_setglobal(L, "Natives");
}

} // namespace

bool Register(lua_State* L) {
    if (!L) {
        return false;
    }

    RegisterNativeTable(L);

    lua_pushcfunction(L, Lua_SetGameBase);
    lua_setglobal(L, "SetGameBase");

    lua_pushcfunction(L, Lua_GetGameBase);
    lua_setglobal(L, "GetGameBase");

    lua_pushcfunction(L, Lua_NativeAddress);
    lua_setglobal(L, "NativeAddress");

    lua_pushcfunction(L, Lua_CallNative);
    lua_setglobal(L, "CallNative");

    lua_pushcfunction(L, Lua_RegisterCharacter);
    lua_setglobal(L, "RegisterCharacter");

    lua_pushcfunction(L, Lua_InstallCharacterExpansion);
    lua_setglobal(L, "InstallCharacterExpansion");

    return true;
}

} // namespace HookCrashers::NativeDB::Lua
#else
namespace HookCrashers::NativeDB::Lua {

bool Register(lua_State*) {
    return false;
}

} // namespace HookCrashers::NativeDB::Lua
#endif
