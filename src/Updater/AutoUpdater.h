#pragma once

#include "../Core/Config.h"

#include <string>

namespace HookCrashers::Updater::AutoUpdater {

bool CheckForUpdates(const std::string& moduleDirectory, const Core::Config::RuntimeConfig& config);
bool HasAvailableUpdate();
bool InstallAvailableUpdate();

} // namespace HookCrashers::Updater::AutoUpdater

