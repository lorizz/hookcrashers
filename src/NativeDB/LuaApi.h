#pragma once

#include <string>

struct lua_State;

namespace HookCrashers::NativeDB::Lua {

// Registers:
//   Natives.CC_Name = rva
//   CallNative(nativeOrName, ...)
// Arguments are passed as integer-sized raw values. This is intended for
// exploratory glue; stable mod code should wrap each native with typed C++.
bool Register(lua_State* state);
void SetCurrentModDirectory(const std::string& directory);
void SetCurrentModContext(const std::string& directory, const std::string& version);

} // namespace HookCrashers::NativeDB::Lua
