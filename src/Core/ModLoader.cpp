#include "ModLoader.h"
#include "../Util/Logger.h"
#include <windows.h> // For FindFirstFile, etc.
#include <string>
#include <vector>
#include <shlwapi.h> // For PathIsDirectory
#include <pathcch.h> // For PathCchCombine

#pragma comment(lib, "shlwapi.lib") // Link against the Shell Light-weight Utility library
#pragma comment(lib, "pathcch.lib") // Link against the Path CCH library

typedef const char* (*GetModInfo_t)();
typedef bool (*InitializeMod_t)();

namespace HookCrashers {
    namespace Core {
        std::vector<ModInfo> ModLoader::s_loadedMods;

        size_t ModLoader::GetModCount() {
            return s_loadedMods.size();
        }

        const ModInfo* ModLoader::GetModInfoByIndex(size_t index) {
            Util::Logger& L = Util::Logger::Instance();

            if (index >= s_loadedMods.size()) {
                return nullptr;
            }

            const ModInfo* result = &s_loadedMods[index];
            return result;
        }

        void ModLoader::LoadAllMods() {
            Util::Logger& L = Util::Logger::Instance();
            const wchar_t* modDir = L"mods"; // Use wide characters for Windows API

            // Check if the Mods directory exists
            if (PathFileExistsW(modDir) == FALSE) {
                L.Get()->info("[ModLoader] 'mods' directory not found, creating it.");
                if (CreateDirectoryW(modDir, NULL) == FALSE) {
                    L.Get()->error("[ModLoader] Failed to create 'mods' directory. Error code: {}", GetLastError());
                    return;
                }
                L.Get()->info("[ModLoader] 'mods' directory created. No mods to load.");
                return; // Nothing to load
            }

            if (PathIsDirectoryW(modDir) == FALSE) {
                L.Get()->error("[ModLoader] 'mods' exists but is not a directory!");
                return;
            }

            L.Get()->info("[ModLoader] Scanning for mods in 'mods' directory...");

            wchar_t searchPath[MAX_PATH];
            PathCchCombine(searchPath, MAX_PATH, modDir, L"*.asi"); // Also finds .dll

            WIN32_FIND_DATAW findData;
            HANDLE hFind = FindFirstFileW(searchPath, &findData);

            if (hFind == INVALID_HANDLE_VALUE) {
                // This is normal if the directory is empty.
                if (GetLastError() == ERROR_FILE_NOT_FOUND) {
                    L.Get()->info("[ModLoader] No .asi or .dll files found in 'mods' directory.");
                }
                else {
                    L.Get()->error("[ModLoader] FindFirstFile failed. Error code: {}", GetLastError());
                }
                L.Get()->info("------------------------------------------------------------");
                L.Get()->info("[ModLoader] Finished scanning for mods.");
                return;
            }

            do {
                // Skip directories
                if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    wchar_t fullPath[MAX_PATH];
                    PathCchCombine(fullPath, MAX_PATH, modDir, findData.cFileName);

                    // Convert wide-char path to narrow-char for logging
                    char narrowFileName[MAX_PATH];
                    size_t charsConverted = 0;
                    wcstombs_s(&charsConverted, narrowFileName, MAX_PATH, findData.cFileName, _TRUNCATE);

                    L.Get()->info("------------------------------------------------------------");
                    L.Get()->info("[ModLoader] Found potential mod: {}", narrowFileName);

                    // Attempt to load the library using the full wide-char path
                    HMODULE hModule = LoadLibraryW(fullPath);
                    if (!hModule) {
                        L.Get()->error("[ModLoader] Failed to load library. Error code: {}", GetLastError());
                        continue;
                    }

                    // Get the exported functions (these use narrow-char names)
                    GetModInfo_t getModName = (GetModInfo_t)GetProcAddress(hModule, "GetModName");
                    GetModInfo_t getModAuthor = (GetModInfo_t)GetProcAddress(hModule, "GetModAuthor");
                    GetModInfo_t getModVersion = (GetModInfo_t)GetProcAddress(hModule, "GetModVersion");
                    InitializeMod_t initializeMod = (InitializeMod_t)GetProcAddress(hModule, "InitializeMod");

                    // A valid mod MUST export InitializeMod and GetModName.
                    if (!initializeMod || !getModName) {
                        L.Get()->warn("[ModLoader] '{}' is not a valid HookCrashers mod (missing required exports). Unloading.", narrowFileName);
                        FreeLibrary(hModule);
                        continue;
                    }

                    // Log mod details
                    L.Get()->info("  Name:    {}", getModName());
                    if (getModAuthor) L.Get()->info("  Author:  {}", getModAuthor());
                    if (getModVersion) L.Get()->info("  Version: {}", getModVersion());

                    // Initialize the mod
                    L.Get()->info("[ModLoader] Initializing '{}'...", getModName());
                    bool success = false;
                    try {
                        success = initializeMod();
                    }
                    catch (const std::exception& e) {
                        L.Get()->error("[ModLoader] Exception during initialization of '{}': {}", getModName(), e.what());
                        success = false;
                    }
                    catch (...) {
                        L.Get()->error("[ModLoader] UNKNOWN exception during initialization of '{}'!", getModName());
                        success = false;
                    }

                    if (success) {
                        L.Get()->info("[ModLoader] Successfully initialized '{}'.", getModName());
                        ModInfo info;
                        info.name = getModName();
                        info.author = getModAuthor ? getModAuthor() : "N/A";
                        info.version = getModVersion ? getModVersion() : "N/A";
                        info.handle = hModule;
                        s_loadedMods.push_back(info);
                    }
                    else {
                        L.Get()->error("[ModLoader] Failed to initialize '{}'. Unloading.", getModName());
                        FreeLibrary(hModule); // Unload on failure
                    }
                }
            } while (FindNextFileW(hFind, &findData) != 0);

            FindClose(hFind);

            L.Get()->info("------------------------------------------------------------");
            L.Get()->info("[ModLoader] Finished scanning for mods.");
        }
    }
}