#pragma once

#include <string>

namespace HookCrashers::Localization {

bool Install();
void Remove();
void Reset();
bool LoadFile(const std::string& path, const std::string& sourceName);
bool Resolve(const std::string& logicalId, std::string& value);

} // namespace HookCrashers::Localization
