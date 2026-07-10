#pragma once

struct lua_State;

namespace HookCrashers::NativeDB::Lua {

// Registers:
//   Natives.CC_Name = rva
//   CallNative(nativeOrName, ...)
// Arguments are passed as integer-sized raw values. This is intended for
// exploratory glue; stable mod code should wrap each native with typed C++.
bool Register(lua_State* state);

} // namespace HookCrashers::NativeDB::Lua
