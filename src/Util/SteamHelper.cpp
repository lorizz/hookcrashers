#include "SteamHelper.h"
#include <Windows.h>
#include <string>
#include <vector>
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
                // Non è un errore grave, l'utente potrebbe non aver lanciato il gioco da Steam
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
                Logger::Instance().Get()->warn("'ActiveUser' è 0, indica nessun utente attivo.");
                return { false, 0 };
            }

            return { true, activeUserId };
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
                L.Get()->error("La cartella 'userdata' di Steam non è stata trovata in: {}", userdataDir);
                return { false, "" };
            }

            // --- NUOVA LOGICA: CERCA PRIMA L'UTENTE ATTIVO ---
            auto activeUserResult = GetActiveSteamUser();
            if (activeUserResult.first) {
                uint32_t activeUserId = activeUserResult.second;
                L.Get()->info("Trovato utente Steam attivo con ID: {}", activeUserId);
                std::string savePath = userdataDir + "\\" + std::to_string(activeUserId) + "\\204360\\remote\\cc_save.dat";

                if (PathExists(savePath)) {
                    L.Get()->info("Trovato file di salvataggio per l'utente attivo: {}", savePath);
                    return { true, savePath };
                }
                else {
                    L.Get()->warn("Il salvataggio per l'utente attivo non è stato trovato in: {}", savePath);
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
                    std::string savePath = userdataDir + "\\" + findData.cFileName + "\\204360\\remote\\cc_save.dat";
                    if (PathExists(savePath)) {
                        L.Get()->info("Trovato file di salvataggio (fallback): {}", savePath);
                        FindClose(hFind);
                        return { true, savePath };
                    }
                }
            } while (FindNextFileA(hFind, &findData) != 0);

            FindClose(hFind);

            L.Get()->error("Nessun file 'cc_save.dat' trovato per l'AppID 204360 in nessuna cartella utente di Steam.");
            return { false, "" };
        }
    }
}