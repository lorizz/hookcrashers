#include "SteamHelper.h"
#include "../Save/SavePatches.h"
#include <Windows.h>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cctype>
#include "Logger.h"

namespace HookCrashers {
    namespace Util {

        std::pair<bool, std::string> GetSteamInstallPath() {
            HKEY hKey;
            if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
                Logger::Instance().Get()->error("Impossibile aprire la chiave di registro di Steam.");
                return { false, "" };
            }

            char steamPath[MAX_PATH];
            DWORD bufferSize = sizeof(steamPath);
            if (RegQueryValueExA(hKey, "SteamPath", nullptr, nullptr, (LPBYTE)steamPath, &bufferSize) != ERROR_SUCCESS) {
                RegCloseKey(hKey);
                Logger::Instance().Get()->error("Impossibile leggere il valore 'SteamPath' dal registro.");
                return { false, "" };
            }

            RegCloseKey(hKey);
            return { true, std::string(steamPath) };
        }

        std::pair<bool, uint32_t> GetActiveSteamUser() {
            HKEY hKey;
            if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam\\ActiveProcess", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
                // Non ? un errore grave, l'utente potrebbe non aver lanciato il gioco da Steam
                Logger::Instance().Get()->warn("Chiave 'ActiveProcess' non trovata. Impossibile determinare l'utente Steam attivo.");
                return { false, 0 };
            }

            DWORD activeUserId = 0;
            DWORD bufferSize = sizeof(activeUserId);
            if (RegQueryValueExA(hKey, "ActiveUser", nullptr, nullptr, (LPBYTE)&activeUserId, &bufferSize) != ERROR_SUCCESS) {
                RegCloseKey(hKey);
                Logger::Instance().Get()->warn("Impossibile leggere il valore 'ActiveUser' dal registro.");
                return { false, 0 };
            }

            RegCloseKey(hKey);

            if (activeUserId == 0) {
                Logger::Instance().Get()->warn("'ActiveUser' ? 0, indica nessun utente attivo.");
                return { false, 0 };
            }

            return { true, activeUserId };
        }

        static std::string NormalizeSteamLanguage(std::string language) {
            language.erase(std::remove_if(language.begin(), language.end(), [](unsigned char ch) {
                return ch == '\0' || ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ' || ch == '"';
            }), language.end());
            std::transform(language.begin(), language.end(), language.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return language;
        }

        static std::pair<bool, std::string> ReadRegistryString(HKEY root, const char* subKey, const char* valueName) {
            HKEY hKey;
            if (RegOpenKeyExA(root, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
                return { false, "" };
            }

            char buffer[256] = {};
            DWORD type = 0;
            DWORD bufferSize = sizeof(buffer);
            const LONG result = RegQueryValueExA(hKey, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &bufferSize);
            RegCloseKey(hKey);

            if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
                return { false, "" };
            }

            std::string value = NormalizeSteamLanguage(buffer);
            return { !value.empty(), value };
        }

        static std::pair<bool, std::string> ReadLanguageFromQuotedKeyValueFile(const std::string& path) {
            std::ifstream file(path);
            if (!file.is_open()) {
                return { false, "" };
            }

            std::string line;
            while (std::getline(file, line)) {
                std::string lower = line;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });

                const size_t keyPos = lower.find("\"language\"");
                if (keyPos == std::string::npos) {
                    continue;
                }

                const size_t firstQuote = line.find('"', keyPos + 10);
                if (firstQuote == std::string::npos) {
                    continue;
                }
                const size_t secondQuote = line.find('"', firstQuote + 1);
                if (secondQuote == std::string::npos) {
                    continue;
                }

                std::string value = NormalizeSteamLanguage(line.substr(firstQuote + 1, secondQuote - firstQuote - 1));
                if (!value.empty()) {
                    return { true, value };
                }
            }

            return { false, "" };
        }

        std::pair<bool, std::string> GetSteamLanguage() {
            Logger& L = Logger::Instance();

            auto appLanguage = ReadRegistryString(HKEY_CURRENT_USER, "Software\\Valve\\Steam\\Apps\\204360", "Language");
            if (appLanguage.first) {
                L.Get()->debug("[SteamHelper] Castle Crashers app language from registry: '{}'.", appLanguage.second);
                return appLanguage;
            }
            L.Get()->debug("[SteamHelper] Castle Crashers app registry language not found.");

            auto steamLanguage = ReadRegistryString(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "Language");
            if (steamLanguage.first) {
                L.Get()->debug("[SteamHelper] Steam language from registry: '{}'.", steamLanguage.second);
                return steamLanguage;
            }
            L.Get()->debug("[SteamHelper] Steam registry language not found.");

            auto steamPath = GetSteamInstallPath();
            if (!steamPath.first) {
                L.Get()->warn("[SteamHelper] Cannot read Steam language because Steam install path is unavailable.");
                return { false, "" };
            }

            const std::string appManifestPath = steamPath.second + "\\steamapps\\appmanifest_204360.acf";
            auto manifestLanguage = ReadLanguageFromQuotedKeyValueFile(appManifestPath);
            if (manifestLanguage.first) {
                L.Get()->debug("[SteamHelper] Castle Crashers app language from '{}': '{}'.", appManifestPath, manifestLanguage.second);
                return manifestLanguage;
            }
            L.Get()->debug("[SteamHelper] No language entry found in '{}'.", appManifestPath);

            const std::string configPath = steamPath.second + "\\config\\config.vdf";
            auto configLanguage = ReadLanguageFromQuotedKeyValueFile(configPath);
            if (configLanguage.first) {
                L.Get()->debug("[SteamHelper] Steam language from '{}': '{}'.", configPath, configLanguage.second);
                return configLanguage;
            }
            L.Get()->warn("[SteamHelper] Steam language not found in registry or config files.");
            return { false, "" };
        }
        bool PathExists(const std::string& path) {
            DWORD fileAttributes = GetFileAttributesA(path.c_str());
            return (fileAttributes != INVALID_FILE_ATTRIBUTES);
        }

        std::pair<bool, std::string> FindCastleCrashersSavePath() {
            Logger& L = Logger::Instance();

            auto steamPathResult = GetSteamInstallPath();
            if (!steamPathResult.first) {
                L.Get()->error("Percorso di installazione di Steam non trovato.");
                return { false, "" };
            }

            std::string userdataDir = steamPathResult.second + "\\userdata";
            if (!PathExists(userdataDir)) {
                L.Get()->error("La cartella 'userdata' di Steam non ? stata trovata in: {}", userdataDir);
                return { false, "" };
            }

            // --- NUOVA LOGICA: CERCA PRIMA L'UTENTE ATTIVO ---
            auto activeUserResult = GetActiveSteamUser();
            if (activeUserResult.first) {
                uint32_t activeUserId = activeUserResult.second;
                L.Get()->debug("Trovato utente Steam attivo con ID: {}", activeUserId);
                std::string savePath = userdataDir + "\\" + std::to_string(activeUserId) + "\\204360\\remote\\" + HookCrashers::Save::GetSaveFileName();

                if (PathExists(savePath)) {
                    L.Get()->debug("Trovato file di salvataggio per l'utente attivo: {}", savePath);
                    return { true, savePath };
                }
                else {
                    L.Get()->warn("Il salvataggio per l'utente attivo non ? stato trovato in: {}", savePath);
                    L.Get()->warn("Eseguo la ricerca su tutti gli utenti come fallback...");
                }
            }

            // --- LOGICA DI FALLBACK: CERCA IN TUTTE LE CARTELLE UTENTE ---
            std::string searchPath = userdataDir + "\\*";
            WIN32_FIND_DATAA findData;
            HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

            if (hFind == INVALID_HANDLE_VALUE) {
                L.Get()->error("Impossibile iterare nella cartella 'userdata': {}", userdataDir);
                return { false, "" };
            }

            do {
                if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    (strcmp(findData.cFileName, ".") != 0) &&
                    (strcmp(findData.cFileName, "..") != 0))
                {
                    std::string savePath = userdataDir + "\\" + findData.cFileName + "\\204360\\remote\\" + HookCrashers::Save::GetSaveFileName();
                    if (PathExists(savePath)) {
                        L.Get()->debug("Trovato file di salvataggio (fallback): {}", savePath);
                        FindClose(hFind);
                        return { true, savePath };
                    }
                }
            } while (FindNextFileA(hFind, &findData) != 0);

            FindClose(hFind);

            L.Get()->error("Nessun file save moddato trovato per l'AppID 204360 in nessuna cartella utente di Steam.");
            return { false, "" };
        }
    }
}
