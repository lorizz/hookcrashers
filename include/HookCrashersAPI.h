#pragma once

#include "HookCrashers/Public/Types.h"
#include "HookCrashers/Public/NativeCaller.h"
#include "HookCrashers/Public/NativeFunctions.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

#ifdef HOOKCRASHERS_EXPORTS
    #define HOOKCRASHERS_API __declspec(dllexport)
#else
    #define HOOKCRASHERS_API __declspec(dllimport)
#endif

#define HOOKCRASHERS_API_VERSION 2.0f

using HC_SWFArgument = HookCrashers::SWF::Data::SWFArgument;
using HC_SWFReturn = HookCrashers::SWF::Data::SWFReturn;
using HC_SWFFunctionID = HookCrashers::SWF::Data::SWFFunctionID;

// ============================================================================
// C-Style ABI (Application Binary Interface)
// This is the stable, exported part of the API.
// ============================================================================
extern "C" {
    // --- Core Functions ---
    HOOKCRASHERS_API bool HookCrashers_Initialize(uintptr_t moduleBase);
    HOOKCRASHERS_API float HookCrashers_GetVersion();
    HOOKCRASHERS_API bool HookCrashers_IsInitialized();
    HOOKCRASHERS_API uintptr_t* HookCrashers_GetGameManagerPtr();
    HOOKCRASHERS_API uintptr_t HookCrashers_GetModuleBase();

    // --- String Management ---
    HOOKCRASHERS_API uint16_t HookCrashers_AddString(const char* stringToAdd);
    HOOKCRASHERS_API size_t HookCrashers_GetString(uint16_t stringId, char* buffer, size_t bufferSize);

	// --- Player Management ---
	HOOKCRASHERS_API uint32_t HookCrashers_IsFeatureEnabled(uint16_t featureId);
    HOOKCRASHERS_API void* HookCrashers_GetPlayerObject(int playerIndex);
    HOOKCRASHERS_API char HookCrashers_GetPlayerState(void* playerObject);
    HOOKCRASHERS_API char HookCrashers_GetPlayerActiveState(void* playerObj);
    HOOKCRASHERS_API uint64_t HookCrashers_GetPlayerPosition(void* playerObject);
    HOOKCRASHERS_API int HookCrashers_GetPlayerSelectedCharacterType(void* playerObject);
    HOOKCRASHERS_API bool HookCrashers_IsOnlineMode();

    // --- Custom SWF Function Registration ---
    typedef void (*HookCrashers_CustomSWFCallback)(void* userData, int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn);
    HOOKCRASHERS_API bool HookCrashers_RegisterCustomSWF(uint16_t functionId, const char* functionName, HookCrashers_CustomSWFCallback callback, void* userData);

    // --- SWF Function Override System ---
    typedef void (*HookCrashers_OverrideCallback)(void* thisPtr, int swfContext, uint32_t functionIdRaw, int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn, uint32_t callbackPtr);
    HOOKCRASHERS_API bool HookCrashers_RegisterOverride(uint16_t functionId, HookCrashers_OverrideCallback callback);
    HOOKCRASHERS_API void HookCrashers_CallOriginal(void* thisPtr, int swfContext, uint32_t functionIdRaw, int paramCount, HC_SWFArgument** swfArgs, uint32_t* swfReturnRaw, uint32_t callbackPtr);

    // --- Logging ---
    HOOKCRASHERS_API void HookCrashers_LogInfo(const char* message);
    HOOKCRASHERS_API void HookCrashers_LogWarn(const char* message);
    HOOKCRASHERS_API void HookCrashers_LogError(const char* message);

    // --- Return Value Helpers ---
    HOOKCRASHERS_API void HookCrashers_SetReturnBool(HC_SWFReturn* swfReturn, bool value);
    HOOKCRASHERS_API void HookCrashers_SetReturnInt(HC_SWFReturn* swfReturn, int32_t value);
    HOOKCRASHERS_API void HookCrashers_SetReturnFloat(HC_SWFReturn* swfReturn, float value);
    HOOKCRASHERS_API void HookCrashers_SetReturnString(HC_SWFReturn* swfReturn, uint16_t stringId);
    HOOKCRASHERS_API void HookCrashers_SetReturnFailure(HC_SWFReturn* swfReturn);

    // --- SWF Argument Reading ---
    HOOKCRASHERS_API int32_t HookCrashers_Arg_GetInteger(const HC_SWFArgument* arg, int32_t defaultVal);
    HOOKCRASHERS_API bool HookCrashers_Arg_GetBoolean(const HC_SWFArgument* arg, bool defaultVal);
    HOOKCRASHERS_API float HookCrashers_Arg_GetFloat(const HC_SWFArgument* arg, float defaultVal);
    HOOKCRASHERS_API uint16_t HookCrashers_Arg_GetStringId(const HC_SWFArgument* arg, uint16_t defaultVal);
    // Per ottenere la stringa, si usa prima GetStringId e poi GetString (come definito prima)
    HOOKCRASHERS_API size_t HookCrashers_GetString(uint16_t stringId, char* buffer, size_t bufferSize);

    // --- Memory Helpers ---
    HOOKCRASHERS_API bool HookCrashers_PatchBytes(uintptr_t address, const std::vector<uint8_t>& newBytes);
}

// ============================================================================
// C++ Wrapper for Easier Integration
// ============================================================================
#ifdef __cplusplus
namespace HookCrashers {
    namespace API {
        // C++-friendly callback types
        using CustomSWFFunction = std::function<void(int, HC_SWFArgument**, HC_SWFReturn*)>;
        using OverrideFunction = std::function<void(void*, int, uint32_t, int, HC_SWFArgument**, HC_SWFReturn*, uint32_t)>;

        // Internal structs to hold the C++ function objects for the callbacks
        struct InternalCustomCallback { CustomSWFFunction func; };
        struct InternalOverrideCallback { OverrideFunction func; };

        class Client {
        private:
            // Static maps to hold callbacks for the C++ wrapper
            static std::unordered_map<uint16_t, InternalCustomCallback> s_customCallbacks;
            static std::unordered_map<uint16_t, InternalOverrideCallback> s_overrideCallbacks;

            // Trampoline for custom functions
            static void CustomCallbackTrampoline(void* userData, int p, HC_SWFArgument** a, HC_SWFReturn* r) {
                if (auto* cb = static_cast<InternalCustomCallback*>(userData)) { if (cb->func) cb->func(p, a, r); }
            }

            // Trampoline for override functions
            static void OverrideCallbackTrampoline(void* t, int s, uint32_t f, int p, HC_SWFArgument** a, HC_SWFReturn* r, uint32_t c) {
                auto it = s_overrideCallbacks.find(f & 0xFFFF);
                if (it != s_overrideCallbacks.end()) {
                    if (it->second.func) {
                        it->second.func(t, s, f, p, a, r, c);
                    }
                }
            }

        public:
            static bool Initialize(uintptr_t moduleBase) { return HookCrashers_Initialize(moduleBase); }
            static float GetVersion() { return HookCrashers_GetVersion(); }
            static bool IsInitialized() { return HookCrashers_IsInitialized(); }
            static uintptr_t* GetGameManagerPtr() { return HookCrashers_GetGameManagerPtr(); }
            static uintptr_t GetModuleBase() { return HookCrashers_GetModuleBase(); }

            static uint16_t AddString(const std::string& str) {
                return HookCrashers_AddString(str.c_str());
            }

            static uint32_t IsFeatureEnabled(uint16_t featureId) {
                return HookCrashers_IsFeatureEnabled(featureId);
			}

            static void* GetPlayerObject(uint16_t playerId) {
                return HookCrashers_GetPlayerObject(playerId);
            }

            static char GetPlayerState(void* playerObject) {
                return HookCrashers_GetPlayerState(playerObject);
            }

            static char GetPlayerActiveState(void* playerObject) {
                return HookCrashers_GetPlayerActiveState(playerObject);
            }

            static uint64_t GetPlayerPosition(void* playerObject) {
                return HookCrashers_GetPlayerPosition(playerObject);
            }

            static int GetPlayerSelectedCharacterType(void* playerObject) {
                return HookCrashers_GetPlayerSelectedCharacterType(playerObject);
            }

            static bool IsOnlineMode() {
                return HookCrashers_IsOnlineMode();
            }

            static std::string GetString(uint16_t stringId) {
                size_t requiredSize = HookCrashers_GetString(stringId, nullptr, 0);
                if (requiredSize == 0) return "";
                std::string result(requiredSize, '\0');
                HookCrashers_GetString(stringId, &result[0], result.size() + 1);
                return result;
            }

            static bool RegisterCustomSWF(uint16_t id, const std::string& name, CustomSWFFunction cb) {
                s_customCallbacks[id] = { cb };
                return HookCrashers_RegisterCustomSWF(id, name.c_str(), CustomCallbackTrampoline, &s_customCallbacks[id]);
            }

            static bool RegisterOverride(uint16_t id, OverrideFunction cb) {
                s_overrideCallbacks[id] = { cb };
                return HookCrashers_RegisterOverride(id, OverrideCallbackTrampoline);
            }

            static void CallOriginal(void* t, int s, uint32_t f, int p, HC_SWFArgument** a, uint32_t* r, uint32_t c) {
                HookCrashers_CallOriginal(t, s, f, p, a, r, c);
            }

            static void LogInfo(const std::string& msg) { HookCrashers_LogInfo(msg.c_str()); }
            static void LogWarn(const std::string& msg) { HookCrashers_LogWarn(msg.c_str()); }
            static void LogError(const std::string& msg) { HookCrashers_LogError(msg.c_str()); }

            template <typename Ret = void, typename... Args>
            static Ret CallNative(const Native::NativeInfo<void*>& native, Args... args) {
                return Native::CallNative<Ret, Args...>(native, args...);
            }
        };

        class ReturnHelper {
        public:
            static void SetBool(HC_SWFReturn* ret, bool value) { HookCrashers_SetReturnBool(ret, value); }
            static void SetInt(HC_SWFReturn* ret, int32_t value) { HookCrashers_SetReturnInt(ret, value); }
            static void SetFloat(HC_SWFReturn* ret, float value) { HookCrashers_SetReturnFloat(ret, value); }
            static void SetString(HC_SWFReturn* ret, const std::string& value) {
                uint16_t id = Client::AddString(value);
                HookCrashers_SetReturnString(ret, id);
            }
            static void SetFailure(HC_SWFReturn* ret) { HookCrashers_SetReturnFailure(ret); }
        };

        class SWFArgumentReader {
        public:
            static int32_t GetInteger(const HC_SWFArgument* arg, int32_t defaultVal = 0) {
                return HookCrashers_Arg_GetInteger(arg, defaultVal);
            }

            static bool GetBoolean(const HC_SWFArgument* arg, bool defaultVal = false) {
                return HookCrashers_Arg_GetBoolean(arg, defaultVal);
            }

            static float GetFloat(const HC_SWFArgument* arg, float defaultVal = 0.0f) {
                return HookCrashers_Arg_GetFloat(arg, defaultVal);
            }

            static uint16_t GetStringId(const HC_SWFArgument* arg, uint16_t defaultVal = 0) {
                return HookCrashers_Arg_GetStringId(arg, defaultVal);
            }

            static std::string GetString(const HC_SWFArgument* arg, const std::string& defaultVal = "") {
                uint16_t id = HookCrashers_Arg_GetStringId(arg, 0);
                if (id == 0) return defaultVal;
                return Client::GetString(id);
            }

            static std::string GetValueAsString(const HC_SWFArgument* arg) {
                if (!arg) return "[Null Arg]";
                switch (arg->type) {
                case HC_SWFArgument::Type::Boolean: return GetBoolean(arg) ? "true" : "false";
                case HC_SWFArgument::Type::Integer: return std::to_string(GetInteger(arg));
                case HC_SWFArgument::Type::Float: {
                    char floatBuf[64];
                    snprintf(floatBuf, sizeof(floatBuf), "%.4f", GetFloat(arg));
                    return std::string(floatBuf);
                }
                case HC_SWFArgument::Type::String:
                    return "\"" + GetString(arg, "[Invalid String ID]") + "\"";
                default:
                    return "[Unknown Type]";
                }
            }
        };

        class MemoryPatcher {
        public:
            static bool PatchBytes(uintptr_t address, const std::vector<uint8_t>& newBytes) {
                return HookCrashers_PatchBytes(address, newBytes);
            }
        };
    }
}
#endif