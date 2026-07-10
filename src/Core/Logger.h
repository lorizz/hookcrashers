#pragma once

#include <cstdarg>
#include <string>

namespace HookCrashers::Core::Logger {

void Initialize(void* moduleHandle);
void Shutdown();
void Info(const char* format, ...);
void Warn(const char* format, ...);
void Error(const char* format, ...);
const std::string& LogPath();
const std::string& ModuleDirectory();

} // namespace HookCrashers::Core::Logger
