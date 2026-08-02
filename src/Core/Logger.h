#pragma once

#include <cstdarg>
#include <string>
#include <vector>

namespace HookCrashers::Core::Logger {

void Initialize(void* moduleHandle);
void Shutdown();
void SetExternalConsoleEnabled(bool enabled);
void Info(const char* format, ...);
void Warn(const char* format, ...);
void Error(const char* format, ...);
const std::string& LogPath();
const std::string& ModuleDirectory();
std::vector<std::string> SnapshotLines();

} // namespace HookCrashers::Core::Logger
