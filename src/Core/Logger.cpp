#include "Logger.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

namespace HookCrashers::Core::Logger {
namespace {

CRITICAL_SECTION g_Lock;
bool g_Initialized = false;
FILE* g_File = nullptr;
std::string g_LogPath;
std::string g_ModuleDirectory;

std::string DirectoryOf(const char* path) {
    if (!path || !path[0]) {
        return ".";
    }

    std::string value(path);
    const std::size_t slash = value.find_last_of("\\/");
    if (slash == std::string::npos) {
        return ".";
    }
    return value.substr(0, slash);
}

void WriteUnlocked(const char* level, const char* format, va_list args) {
    if (!g_File) {
        return;
    }

    SYSTEMTIME time {};
    GetLocalTime(&time);
    std::fprintf(g_File,
                 "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [%s] ",
                 time.wYear,
                 time.wMonth,
                 time.wDay,
                 time.wHour,
                 time.wMinute,
                 time.wSecond,
                 time.wMilliseconds,
                 level);
    std::vfprintf(g_File, format, args);
    std::fputc('\n', g_File);
    std::fflush(g_File);
}

void LogVa(const char* level, const char* format, va_list args) {
    if (!g_Initialized) {
        return;
    }

    EnterCriticalSection(&g_Lock);
    WriteUnlocked(level, format, args);
    LeaveCriticalSection(&g_Lock);
}

} // namespace

void Initialize(void* moduleHandle) {
    if (g_Initialized) {
        return;
    }

    InitializeCriticalSection(&g_Lock);

    char modulePath[MAX_PATH] {};
    GetModuleFileNameA(static_cast<HMODULE>(moduleHandle), modulePath, sizeof(modulePath));
    g_ModuleDirectory = DirectoryOf(modulePath);
    g_LogPath = g_ModuleDirectory + "\\HookCrashers.log";

    fopen_s(&g_File, g_LogPath.c_str(), "a");
    g_Initialized = true;

    Info("Logger initialized");
    Info("Module path: %s", modulePath);
    Info("Module directory: %s", g_ModuleDirectory.c_str());
    Info("Log path: %s", g_LogPath.c_str());
}

void Shutdown() {
    if (!g_Initialized) {
        return;
    }

    Info("Logger shutdown");
    EnterCriticalSection(&g_Lock);
    if (g_File) {
        std::fclose(g_File);
        g_File = nullptr;
    }
    LeaveCriticalSection(&g_Lock);
    DeleteCriticalSection(&g_Lock);
    g_Initialized = false;
}

void Info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogVa("INFO", format, args);
    va_end(args);
}

void Warn(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogVa("WARN", format, args);
    va_end(args);
}

void Error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogVa("ERROR", format, args);
    va_end(args);
}

const std::string& LogPath() {
    return g_LogPath;
}

const std::string& ModuleDirectory() {
    return g_ModuleDirectory;
}

} // namespace HookCrashers::Core::Logger
