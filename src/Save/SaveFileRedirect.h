#pragma once

#include <string>

namespace HookCrashers::Save::SaveFileRedirect {

bool Install(const std::string& manifestName);
const std::string& SaveFileName();
const std::string& BackupFileName();

} // namespace HookCrashers::Save::SaveFileRedirect
