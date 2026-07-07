#include "HookManager.h"
#include "../Util/Logger.h"
#include "../SWF/Custom/CustomFunctions.h"
#include "../SWF/Override/Overrides.h"
#include "../SWF/Dispatcher/Dispatcher.h"
#include "IsFeatureEnabledHook.h"
#include "RegisterSWFFunctionHook.h"
#include "CallSWFFunctionHook.h"
#include "UIInputHandlerHook.h"
#include "RegisterAllSWFFunctionsHook.h"
#include "AddStringHook.h"
#include "SWFDiagnosticsHook.h"
#include "DisplayListDepthHook.h"
#include "../SWF/Helpers/SWFReturnHelper.h"
#include "../SWF/Helpers/SWFArgumentReader.h"
#include "ModLoader.h"
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <winhttp.h>
#include <json.hpp>
#include "GetPlayerObjectHook.h"
#include "DecryptSaveFileHook.h"
#include "BlowfishDecryptHook.h"
#include "../../include/HookCrashers/Public/NativeFunctions.h"
#include "../Save/SaveExpansion.h"
#include "../Save/MenuOnlineCompatibilityHook.h"
#include "../Localization/LocalizationSystem.h"
#include "../Scripting/ScriptModLoader.h"
#include "../UI/ImGuiOverlay.h"

// Define our aliases for the public types
using HC_SWFArgument = HookCrashers::SWF::Data::SWFArgument;
using HC_SWFReturn = HookCrashers::SWF::Data::SWFReturn;
using HC_SWFFunctionID = HookCrashers::SWF::Data::SWFFunctionID;

#pragma comment(lib, "winhttp.lib")

namespace HookCrashers {
    namespace Core {
        static Util::Logger& L = Util::Logger::Instance();

        bool HookManager::s_isInitialized = false;
        uintptr_t HookManager::s_moduleBase = 0;
        float HookManager::s_version = 2.5f; // Good practice to specify 'f' for float literals

        void HelloWorldHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
            Util::Logger::Instance().Get()->debug("Hello, world! This is HookCrashers SWF Function Test.");
		}

        // FIXED: Updated function signature to use the public types
        void GetHookCrashersVersionHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
            std::stringstream ss;
            ss << "HC Ver." << std::fixed << std::setprecision(1) << HookManager::GetVersion();
            uint16_t resStringId = AddCustomString(ss.str().c_str());

            if (swfReturn) {
                // FIXED: Use the static helper function to set the return value
                SWF::Helpers::SWFReturnHelper::SetStringSuccess(swfReturn, resStringId);
            }
        }

        void GetModCountHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
            size_t modCount = ModLoader::GetModCount();
            SWF::Helpers::SWFReturnHelper::SetIntegerSuccess(swfReturn, static_cast<int32_t>(modCount));
        }
        struct MatchmakingLobbyRow {
            std::string hostName;
            int players = 0;
            int maxPlayers = 4;
            int pingFrame = 5;
            bool visible = false;
        };

        std::vector<MatchmakingLobbyRow> g_matchmakingLobbyRows;

        int LobbyIndexFromArgs(int paramCount, HC_SWFArgument** swfArgs) {
            if (paramCount <= 0 || !swfArgs) {
                return 0;
            }
            return SWF::Helpers::SWFArgumentReader::GetInteger(swfArgs[0], 0);
        }

        const MatchmakingLobbyRow* GetCachedLobbyRow(int index) {
            if (index < 0 || index >= static_cast<int>(g_matchmakingLobbyRows.size())) {
                return nullptr;
            }
            return &g_matchmakingLobbyRows[static_cast<size_t>(index)];
        }

        int PingFrameFromLobby(const nlohmann::json& lobby) {
            if (!lobby.contains("pingMs") || lobby["pingMs"].is_null()) {
                return 1;
            }

            const int pingMs = lobby.value("pingMs", -1);
            if (pingMs < 0) {
                return 1;
            }
            if (pingMs >= 180) {
                return 2;
            }
            if (pingMs >= 100) {
                return 3;
            }
            if (pingMs >= 50) {
                return 4;
            }
            return 5;
        }

        void CacheMatchmakingLobbies(const std::string& json) {
            g_matchmakingLobbyRows.clear();
            if (json.empty()) {
                return;
            }
            try {
                nlohmann::json root = nlohmann::json::parse(json);
                const auto& lobbies = root.value("lobbies", nlohmann::json::array());
                for (const auto& lobby : lobbies) {
                    if (g_matchmakingLobbyRows.size() >= 4) {
                        break;
                    }
                    MatchmakingLobbyRow row;
                    row.hostName = lobby.value("hostName", "Host");
                    row.players = lobby.value("players", 0);
                    row.maxPlayers = lobby.value("maxPlayers", 4);
                    row.pingFrame = PingFrameFromLobby(lobby);
                    row.visible = true;
                    g_matchmakingLobbyRows.push_back(row);
                }
            }
            catch (const std::exception& e) {
                L.Get()->warn("[Matchmaking] Failed to parse lobby JSON: {}", e.what());
            }
        }

        std::string FetchMatchmakingJson() {
            constexpr wchar_t kHost[] = L"hookcrashers-matchmaking.thelorizz.workers.dev";
            constexpr wchar_t kPath[] = L"/test-lobbies";

            std::string body;
            HINTERNET session = WinHttpOpen(L"HookCrashers/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!session) {
                return body;
            }
            WinHttpSetTimeouts(session, 3000, 3000, 5000, 5000);

            HINTERNET connect = WinHttpConnect(session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
            if (!connect) {
                WinHttpCloseHandle(session);
                return body;
            }

            HINTERNET request = WinHttpOpenRequest(connect, L"GET", kPath, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (!request) {
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                return body;
            }

            if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr)) {
                DWORD available = 0;
                while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
                    std::vector<char> buffer(static_cast<size_t>(available));
                    DWORD read = 0;
                    if (!WinHttpReadData(request, buffer.data(), available, &read) || read == 0) {
                        break;
                    }
                    body.append(buffer.data(), buffer.data() + read);
                    if (body.size() > 256 * 1024) {
                        break;
                    }
                }
            }

            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return body;
        }

        void FetchLobbiesHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
            std::string json = FetchMatchmakingJson();
            if (json.empty()) {
                json = "{\"lobbies\":[],\"error\":\"fetch_failed\"}";
                L.Get()->warn("[Matchmaking] FetchLobbies failed; returning empty payload.");
            }
            else {
                L.Get()->info("[Matchmaking] FetchLobbies returned {} bytes.", json.size());
            }
            CacheMatchmakingLobbies(json);
            const uint16_t stringId = AddCustomString(json.c_str());
            SWF::Helpers::SWFReturnHelper::SetStringSuccess(swfReturn, stringId);
        }

        void GetLobbyNameHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
            const int index = LobbyIndexFromArgs(paramCount, swfArgs);
            const MatchmakingLobbyRow* row = GetCachedLobbyRow(index);
            const char* value = row && row->visible ? row->hostName.c_str() : "";
            L.Get()->info("[Matchmaking] GetLobbyName index={} value='{}' visible={} cached={}", index, value, row ? row->visible : false, g_matchmakingLobbyRows.size());
            const uint16_t stringId = AddCustomString(value);
            SWF::Helpers::SWFReturnHelper::SetStringSuccess(swfReturn, stringId);
        }

        void GetLobbyPlayersHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
            const int index = LobbyIndexFromArgs(paramCount, swfArgs);
            const MatchmakingLobbyRow* row = GetCachedLobbyRow(index);
            std::string text;
            if (row && row->visible) {
                text = std::to_string(row->players) + "/" + std::to_string(row->maxPlayers);
            }
            L.Get()->info("[Matchmaking] GetLobbyPlayers index={} value='{}' visible={} cached={}", index, text, row ? row->visible : false, g_matchmakingLobbyRows.size());
            const uint16_t stringId = AddCustomString(text.c_str());
            SWF::Helpers::SWFReturnHelper::SetStringSuccess(swfReturn, stringId);
        }

        void GetLobbyPingFrameHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
            const int index = LobbyIndexFromArgs(paramCount, swfArgs);
            const MatchmakingLobbyRow* row = GetCachedLobbyRow(index);
            const int frame = row && row->visible ? row->pingFrame : 1;
            L.Get()->info("[Matchmaking] GetLobbyPingFrame index={} frame={} visible={} cached={}", index, frame, row ? row->visible : false, g_matchmakingLobbyRows.size());
            SWF::Helpers::SWFReturnHelper::SetIntegerSuccess(swfReturn, frame);
        }

        void GetLobbyVisibleHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
            const int index = LobbyIndexFromArgs(paramCount, swfArgs);
            const MatchmakingLobbyRow* row = GetCachedLobbyRow(index);
            const bool visible = row && row->visible;
            L.Get()->info("[Matchmaking] GetLobbyVisible index={} visible={} cached={}", index, visible, g_matchmakingLobbyRows.size());
            SWF::Helpers::SWFReturnHelper::SetBooleanSuccess(swfReturn, visible);
        }

        void GetLobbyCountHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
            int count = 0;
            for (const auto& row : g_matchmakingLobbyRows) {
                if (row.visible) {
                    ++count;
                }
            }
            L.Get()->info("[Matchmaking] GetLobbyCount count={} cached={}", count, g_matchmakingLobbyRows.size());
            SWF::Helpers::SWFReturnHelper::SetIntegerSuccess(swfReturn, count);
        }


        void GetLobbySelectedFrameHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
            const int frame = g_matchmakingLobbyRows.empty() ? 1 : Save::GetPlayerLobbySelectedFrame();
            SWF::Helpers::SWFReturnHelper::SetIntegerSuccess(swfReturn, frame);
        }


        void GetModInfoHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
            if (paramCount < 2) {
                Util::Logger::Instance().Get()->warn("[SWF] GetModInfo called with too few parameters.");
                SWF::Helpers::SWFReturnHelper::SetFailure(swfReturn);
                return;
            }

            int32_t index = SWF::Helpers::SWFArgumentReader::GetInteger(swfArgs[0], -1);
            std::string property = SWF::Helpers::SWFArgumentReader::GetString(swfArgs[1]);

            std::string stringToReturn;
            const ModInfo* mod = ModLoader::GetModInfoByIndex(index);

            if (mod) {
                if (property == "name") {
                    stringToReturn = mod->name;
                    L.Get()->debug("[SWF] GetModInfo resolved name='{}'.", stringToReturn);
                }
                else if (property == "author") {
                    stringToReturn = mod->author;
                    L.Get()->debug("[SWF] GetModInfo resolved author='{}'.", stringToReturn);
                }
                else if (property == "version") {
                    stringToReturn = mod->version;
                    L.Get()->debug("[SWF] GetModInfo resolved version='{}'.", stringToReturn);
                }
                else {
                    L.Get()->debug("[SWF] GetModInfo received unknown property '{}'.", property);
                }
            }
            else {
                L.Get()->debug("[SWF] GetModInfo found no mod at index {}.", index);
            }

            uint16_t resultStringId = AddCustomString(stringToReturn.c_str());
            L.Get()->debug("[SWF] GetModInfo returned string_id={}.", resultStringId);
            SWF::Helpers::SWFReturnHelper::SetStringSuccess(swfReturn, resultStringId);
        }

        bool HookManager::Initialize(uintptr_t moduleBase) {
            if (s_isInitialized) {
                //Util::Logger::Instance().Get()->warn("HookManager::Initialize called again.");
                return true;
            }
            if (moduleBase == 0) {
                //Util::Logger::Instance().Get()->error("HookManager::Initialize: Module base is NULL!");
                return false;
            }

            s_moduleBase = moduleBase;
            Util::Logger& L = Util::Logger::Instance();
            L.Get()->debug("===== HookCrashers initialization started (module_base=0x{:X}) =====", moduleBase);

            // The rest of this function remains the same, as it's orchestrating
            // the other systems that we have already fixed.
            bool success = true;
            constexpr bool kEnableRuntimeSWFInjectionHooks = true;

            //L.Get()->debug("Step 1: Loading Native Functions... (TODO)");
            if (!Native::LoadNatives(moduleBase)) {
                L.Get()->error("Failed to load native functions!");
                success = false;
			}

            //L.Get()->debug("Step 2: Setting up RegisterSWFFunction Hook...");
            if (!SetupRegisterSWFFunctionHook(moduleBase)) {
                L.Get()->error("Failed to setup RegisterSWFFunction hook!");
                success = false;
            }

            //L.Get()->debug("Step 3: Setting up AddString Hook...");
            if (!SetupAddStringHook(moduleBase)) {
                L.Get()->error("Failed to setup AddString hook!");
                success = false;
            }

            //L.Get()->debug("Step 4: Setting up CallSWFFunction Hook...");
            if (!SetupCallSWFFunctionHook(moduleBase)) {
                L.Get()->error("Failed to setup CallSWFFunction hook!");
                success = false;
            }
            if (kEnableRuntimeSWFInjectionHooks) {
                if (!SetupSWFDiagnosticsHook(moduleBase)) {
                    L.Get()->warn("Failed to setup SWF diagnostics hook. Continuing without SWF diagnostics.");
                }

                L.Get()->debug("[SWFInject] Display-list depth hook disabled; portrait.swf shape import uses existing PlaceObject depth.");
            }
            else {
                L.Get()->debug("[SWFInject] Runtime SWF injection hooks disabled.");
            }

            if (!SetupUIInputHandlerHook(moduleBase)) {
                L.Get()->error("Failed to setup UIInputHandler Hook!");
                success = false;
            }

            if (!SetupBlowfishDecryptHook(moduleBase)) {
				L.Get()->error("Failed to setup BlowfishDecrypt Hook!");
                success = false;
            }

            //L.Get()->debug("Step 5a: Initializing Custom SWF Functions System...");
            SWF::Custom::InitializeSystem();

            //L.Get()->debug("Step 5b: Initializing SWF Override System...");
            SWF::Override::InitializeSystem(moduleBase);

            if (!Scripting::ScriptModLoader::Instance().DiscoverAndLoad()) {
                L.Get()->error("Failed to discover script mods!");
                success = false;
            }

            if (!Save::InitializeSaveExpansion()) {
                L.Get()->error("Failed to initialize save expansion subsystem!");
                success = false;
            }

            /*if (!Network::SetupNetworkDiagnosticsHooks(moduleBase)) {
                L.Get()->warn("Failed to install network diagnostic hooks. Continuing without network diagnostics.");
            }*/

            if (!Localization::InitializeLocalizationSystem()) {
                L.Get()->error("Failed to initialize localization subsystem!");
                success = false;
            }

            if (!UI::InitializeOverlay()) {
                L.Get()->warn("Failed to initialize ImGui overlay. Continuing without in-game UI.");
            }

            //L.Get()->debug("Step 6: Setting up RegisterAllSWFFunctions Hook...");
            if (!SetupRegisterAllSWFFunctionsHook(moduleBase)) {
                L.Get()->error("Failed to setup RegisterAllSWFFunctions hook!");
                success = false;
            }

            /*if (!SetupIsFeatureEnabledHook(moduleBase)) {
                L.Get()->error("Failed to setup IsFeatureEnabled hook!");
				success = false;
            }

            if (!SetupGetPlayerObjectHook(moduleBase)) {
                L.Get()->error("Failed to setup GetPlayerObject hook!");
                success = false;
            }*/

            if (!SetupDecryptSaveFileHook(moduleBase)) {
                L.Get()->error("Failed to setup DecryptSaveFile hook!");
				success = false;
            }

            //L.Get()->debug("Step 7: Register custom functions...");
            // This call will now work because GetHookCrashersVersion has the correct signature
            SWF::Custom::Register(HC_SWFFunctionID::HelloWorld, "HelloWorld", HelloWorldHandler);
            SWF::Custom::Register(HC_SWFFunctionID::GetHookCrashersVersion, "GetHookCrashersVersion", GetHookCrashersVersionHandler);
            SWF::Custom::Register(HC_SWFFunctionID::GetModCount, "GetModCount", GetModCountHandler);
            SWF::Custom::Register(HC_SWFFunctionID::GetModInfo, "GetModInfo", GetModInfoHandler);
            SWF::Custom::Register(HC_SWFFunctionID::FetchLobbies, "FetchLobbies", FetchLobbiesHandler);
            SWF::Custom::Register(HC_SWFFunctionID::GetLobbyName, "GetLobbyName", GetLobbyNameHandler);
            SWF::Custom::Register(HC_SWFFunctionID::GetLobbyPlayers, "GetLobbyPlayers", GetLobbyPlayersHandler);
            SWF::Custom::Register(HC_SWFFunctionID::GetLobbyPingFrame, "GetLobbyPingFrame", GetLobbyPingFrameHandler);
            SWF::Custom::Register(HC_SWFFunctionID::GetLobbyVisible, "GetLobbyVisible", GetLobbyVisibleHandler);
            SWF::Custom::Register(HC_SWFFunctionID::GetLobbyCount, "GetLobbyCount", GetLobbyCountHandler);
            SWF::Custom::Register(HC_SWFFunctionID::GetLobbySelectedFrame, "GetLobbySelectedFrame", GetLobbySelectedFrameHandler);

            if (success) {
                L.Get()->debug("===== HookCrashers initialization completed successfully =====");
                s_isInitialized = true;

                L.Get()->debug("===== Registering loaded mods =====");
                for (const auto& mod : Scripting::ScriptModLoader::Instance().GetLoadedMods()) {
                    ModLoader::RegisterScriptMod(
                        mod.name,
                        mod.author,
                        mod.version,
                        mod.directory,
                        mod.hasMainLua,
                        mod.hasLocalizations,
                        mod.hasManifest,
                        mod.hasIcon,
                        mod.iconPath);
                }
                ModLoader::LoadLegacyBinaryMods();
                L.Get()->debug("===== Mod registration finished =====");
            }
            else {
                L.Get()->error("===== HookCrashers initialization failed =====");
                s_isInitialized = false;
            }
            L.Get()->flush();

            return s_isInitialized;
        }
    }
}
