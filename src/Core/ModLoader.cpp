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

namespace {
    std::string NarrowFromWide(const std::wstring& value) {
        if (value.empty()) {
            return {};
        }

        const int required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1) {
            return {};
        }

        std::string result(static_cast<size_t>(required - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), required, nullptr, nullptr);
        return result;
    }

    std::wstring GetHookCrashersModsDirectory() {
        wchar_t modulePath[MAX_PATH] = {};
        HMODULE self = GetModuleHandleA("HookCrashers.asi");
        if (!self) {
            self = GetModuleHandleW(L"HookCrashers.asi");
        }
        if (!self || !GetModuleFileNameW(self, modulePath, MAX_PATH)) {
            return L"mods";
        }

        wchar_t* lastSlash = wcsrchr(modulePath, L'\\');
        if (!lastSlash) {
            return L"mods";
        }
        *(lastSlash + 1) = L'\0';

        wchar_t modsPath[MAX_PATH] = {};
        PathCchCombine(modsPath, MAX_PATH, modulePath, L"mods");
        return modsPath;
    }
}

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
            LoadLegacyBinaryMods();
        }

        void ModLoader::RegisterScriptMod(
            const std::string& name,
            const std::string& author,
            const std::string& version,
            const std::string& path,
            bool hasMainLua,
            bool hasLocalizations,
            bool hasManifest,
            bool hasIcon,
            const std::string& iconPath) {
            ModInfo info;
            info.name = name;
            info.author = author;
            info.version = version;
            info.path = path;
            info.iconPath = iconPath;
            info.isScriptMod = true;
            info.hasMainLua = hasMainLua;
            info.hasLocalizations = hasLocalizations;
            info.hasManifest = hasManifest;
            info.hasIcon = hasIcon;
            info.handle = nullptr;
            s_loadedMods.push_back(info);
            Util::Logger::Instance().Get()->info(
                "[ModLoader] Registered folder mod '{}' author='{}' version='{}' main.lua={} locs.json={} manifest={} icon={}.",
                info.name,
                info.author,
                info.version,
                info.hasMainLua,
                info.hasLocalizations,
                info.hasManifest,
                info.hasIcon);
        }

        void ModLoader::LoadLegacyBinaryMods() {
            Util::Logger& L = Util::Logger::Instance();
            const std::wstring modDir = GetHookCrashersModsDirectory();

            // Check if the Mods directory exists
            if (PathFileExistsW(modDir.c_str()) == FALSE) {
                L.Get()->info("[ModLoader] Mods directory not found at '{}'. Creating it now.", NarrowFromWide(modDir));
                if (CreateDirectoryW(modDir.c_str(), NULL) == FALSE) {
                    L.Get()->error("[ModLoader] Failed to create 'mods' directory. Error code: {}", GetLastError());
                    return;
                }
                L.Get()->info("[ModLoader] Mods directory created. No legacy binary mods to load.");
                return;
            }

            if (PathIsDirectoryW(modDir.c_str()) == FALSE) {
                L.Get()->error("[ModLoader] 'mods' exists but is not a directory!");
                return;
            }

            L.Get()->info("[ModLoader] Scanning '{}' for legacy binary mods.", NarrowFromWide(modDir));

            wchar_t searchPath[MAX_PATH];
            PathCchCombine(searchPath, MAX_PATH, modDir.c_str(), L"*.asi");

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
                    PathCchCombine(fullPath, MAX_PATH, modDir.c_str(), findData.cFileName);

                    // Convert wide-char path to narrow-char for logging
                    char narrowFileName[MAX_PATH];
                    size_t charsConverted = 0;
                    wcstombs_s(&charsConverted, narrowFileName, MAX_PATH, findData.cFileName, _TRUNCATE);

                    L.Get()->info("[ModLoader] Found legacy binary mod candidate '{}'.", narrowFileName);

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
                        L.Get()->warn("[ModLoader] '{}' is not a valid HookCrashers binary mod because required exports are missing. Unloading it.", narrowFileName);
                        FreeLibrary(hModule);
                        continue;
                    }

                    // Log mod details
                    L.Get()->info("[ModLoader] Name='{}'.", getModName());
                    if (getModAuthor) L.Get()->info("[ModLoader] Author='{}'.", getModAuthor());
                    if (getModVersion) L.Get()->info("[ModLoader] Version='{}'.", getModVersion());

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
                        info.path = narrowFileName;
                        info.iconPath.clear();
                        info.isScriptMod = false;
                        info.hasMainLua = false;
                        info.hasLocalizations = false;
                        info.hasManifest = false;
                        info.hasIcon = false;
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

            L.Get()->info("[ModLoader] Finished scanning for legacy binary mods. loaded_mods={}.", s_loadedMods.size());
        }
    }
}
