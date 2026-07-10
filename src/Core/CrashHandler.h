#pragma once

#include <string>

namespace HookCrashers::Core::CrashHandler {

void Install(const std::string& directory);
void Uninstall();

} // namespace HookCrashers::Core::CrashHandler
