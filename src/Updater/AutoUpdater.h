#pragma once

#include "../Core/Config.h"

#include <string>

namespace HookCrashers::Updater::AutoUpdater {

void CheckForUpdates(const std::string& moduleDirectory, const Core::Config::RuntimeConfig& config);

} // namespace HookCrashers::Updater::AutoUpdater

