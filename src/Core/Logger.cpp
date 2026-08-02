#include "Logger.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace HookCrashers::Core::Logger {
namespace {

CRITICAL_SECTION g_Lock;
bool g_Initialized = false;
FILE* g_File = nullptr;
std::string g_LogPath;
std::string g_ModuleDirectory;
std::vector<std::string> g_Lines;
constexpr std::size_t kMaxBufferedLines = 600;

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

void WriteUnlocked(const char* level, const char* message) {
    SYSTEMTIME time {};
    GetLocalTime(&time);
    char line[2300] {};
    std::snprintf(line,
                  sizeof(line),
                  "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [%s] %s",
                  time.wYear,
                  time.wMonth,
                  time.wDay,
                  time.wHour,
                  time.wMinute,
                  time.wSecond,
                  time.wMilliseconds,
                  level,
                  message ? message : "");

    g_Lines.emplace_back(line);
    if (g_Lines.size() > kMaxBufferedLines) {
        g_Lines.erase(g_Lines.begin(), g_Lines.begin() + (g_Lines.size() - kMaxBufferedLines));
    }

    if (g_File) {
        std::fprintf(g_File, "%s\n", line);
        std::fflush(g_File);
    }
}

void LogVa(const char* level, const char* format, va_list args) {
    if (!g_Initialized) {
        return;
    }

    char message[2048] {};
    vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);

    EnterCriticalSection(&g_Lock);
    WriteUnlocked(level, message);
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
    g_Lines.clear();

    fopen_s(&g_File, g_LogPath.c_str(), "w");
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

std::vector<std::string> SnapshotLines() {
    if (!g_Initialized) {
        return {};
    }

    EnterCriticalSection(&g_Lock);
    std::vector<std::string> copy = g_Lines;
    LeaveCriticalSection(&g_Lock);
    return copy;
}

} // namespace HookCrashers::Core::Logger
