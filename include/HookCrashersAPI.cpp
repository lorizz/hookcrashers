#include "../src/stdafx.h"
#include "HookCrashersAPI.h"

// Include ALL necessary internal headers
#include "../src/Core/HookManager.h"
#include "../src/Core/AddStringHook.h"
#include "../src/SWF/Custom/CustomFunctions.h"
#include "../src/SWF/Override/Overrides.h"
#include "../src/SWF/Dispatcher/Dispatcher.h"
#include "../src/Util/Logger.h"
#include "../src/Util/StringCache.h"
#include "../src/Core/IsFeatureEnabledHook.h"
#include "../src/Core/GetPlayerObjectHook.h"
#include "../src/Util/MemoryPatcher.h"
#include "HookCrashers/Public/Globals.h"

// Static map definitions for the C++ wrapper's callbacks
std::unordered_map<uint16_t, HookCrashers::API::InternalCustomCallback> HookCrashers::API::Client::s_customCallbacks;
std::unordered_map<uint16_t, HookCrashers::API::InternalOverrideCallback> HookCrashers::API::Client::s_overrideCallbacks;

// A map to store C-style callbacks registered by non-C++ consumers (e.g. C or other languages)
struct CStyleCallbackInfo {
    HookCrashers_CustomSWFCallback callback;
    void* userData;
};
static std::unordered_map<uint16_t, CStyleCallbackInfo> s_c_callbacks;

extern "C" {
    // --- Core Functions ---
    HOOKCRASHERS_API bool HookCrashers_Initialize(uintptr_t moduleBase) {
        return HookCrashers::Core::HookManager::Initialize(moduleBase);
    }

    HOOKCRASHERS_API float HookCrashers_GetVersion() {
        return HookCrashers::Core::HookManager::GetVersion();
    }

    HOOKCRASHERS_API bool HookCrashers_IsInitialized() {
        return HookCrashers::Core::HookManager::IsInitialized();
    }

    HOOKCRASHERS_API uintptr_t* HookCrashers_GetGameManagerPtr() {
        return HookCrashers::Core::g_pGameManagerPtr;
    }

    HOOKCRASHERS_API uintptr_t HookCrashers_GetModuleBase() {
        return g_moduleBase;
    }

    // --- String Management ---
    HOOKCRASHERS_API uint16_t HookCrashers_AddString(const char* stringToAdd) {
        return HookCrashers::Core::AddCustomString(stringToAdd);
    }

    HOOKCRASHERS_API size_t HookCrashers_GetString(uint16_t stringId, char* buffer, size_t bufferSize) {
        std::string str = HookCrashers::Util::StringCache::GetStringCopyById(stringId);
        if (buffer == nullptr) {
            return str.length();
        }
        if (bufferSize > str.length()) {
            strcpy_s(buffer, bufferSize, str.c_str());
            return str.length();
        }
        return 0;
    }

    // --- Player Management ---
    HOOKCRASHERS_API uint32_t HookCrashers_IsFeatureEnabled(uint16_t featureId) {
        return HookCrashers::Core::GetIsFeatureEnabled(featureId);
	}

    HOOKCRASHERS_API void* HookCrashers_GetPlayerObject(int playerIndex) {
        return HookCrashers::Core::GetPlayerObject(playerIndex);
    }

    HOOKCRASHERS_API char HookCrashers_GetPlayerState(void* playerObject) {
        return HookCrashers::Core::GetPlayerState(playerObject);
    }

    HOOKCRASHERS_API char HookCrashers_GetPlayerActiveState(void* playerObject) {
        return HookCrashers::Core::GetPlayerActiveState(playerObject);
    }

    HOOKCRASHERS_API uint64_t HookCrashers_GetPlayerPosition(void* playerObject) {
        return HookCrashers::Core::GetPlayerPosition(playerObject);
    }

    HOOKCRASHERS_API int HookCrashers_GetPlayerSelectedCharacterType(void* playerObject) {
        return HookCrashers::Core::GetPlayerSelectedCharacterType(playerObject);
    }

    HOOKCRASHERS_API bool HookCrashers_IsOnlineMode() {
        return HookCrashers::Core::IsOnlineMode();
    }

    // --- Custom SWF Function Registration ---
    HOOKCRASHERS_API bool HookCrashers_RegisterCustomSWF(uint16_t functionId, const char* functionName, HookCrashers_CustomSWFCallback callback, void* userData) {
        if (!callback) return false;

        s_c_callbacks[functionId] = { callback, userData };

        auto internalCallback = [functionId](int p, HC_SWFArgument** a, HC_SWFReturn* r) {
            auto it = s_c_callbacks.find(functionId);
            if (it != s_c_callbacks.end()) {
                it->second.callback(it->second.userData, p, a, r);
            }
            };
        return HookCrashers::SWF::Custom::Register(functionId, functionName, internalCallback);
    }

    // --- SWF Function Override System ---
    HOOKCRASHERS_API bool HookCrashers_RegisterOverride(uint16_t functionId, HookCrashers_OverrideCallback callback) {
        if (!callback) return false;
        return HookCrashers::SWF::Override::Register(functionId, callback);
    }

    HOOKCRASHERS_API void HookCrashers_CallOriginal(void* thisPtr, int swfContext, uint32_t functionIdRaw, int paramCount, HC_SWFArgument** swfArgs, uint32_t* swfReturnRaw, uint32_t callbackPtr) {
        HookCrashers::SWF::Dispatcher::CallOriginal(thisPtr, swfContext, functionIdRaw, paramCount, swfArgs, swfReturnRaw, callbackPtr);
    }

    // --- Logging ---
    HOOKCRASHERS_API void HookCrashers_LogInfo(const char* message) {
        if (message) HookCrashers::Util::Logger::Instance().Get()->info(message);
    }

    HOOKCRASHERS_API void HookCrashers_LogWarn(const char* message) {
        if (message) HookCrashers::Util::Logger::Instance().Get()->warn(message);
    }

    HOOKCRASHERS_API void HookCrashers_LogError(const char* message) {
        if (message) HookCrashers::Util::Logger::Instance().Get()->error(message);
    }

    // --- Return Value Helpers ---
    HOOKCRASHERS_API void HookCrashers_SetReturnBool(HC_SWFReturn* swfReturn, bool value) {
        if (!swfReturn) return;
        swfReturn->status = 0;
        swfReturn->type = HC_SWFReturn::Type::Boolean;
        swfReturn->padding = 0;
        swfReturn->value.rawValue = 0;
        swfReturn->value.boolValue = static_cast<uint8_t>(value ? 1 : 0);
    }

    HOOKCRASHERS_API void HookCrashers_SetReturnInt(HC_SWFReturn* swfReturn, int32_t value) {
        if (!swfReturn) return;
        swfReturn->status = 0;
        swfReturn->type = HC_SWFReturn::Type::Integer;
        swfReturn->padding = 0;
        swfReturn->value.intValue = value;
    }

    HOOKCRASHERS_API void HookCrashers_SetReturnFloat(HC_SWFReturn* swfReturn, float value) {
        if (!swfReturn) return;
        swfReturn->status = 0;
        swfReturn->type = HC_SWFReturn::Type::Float;
        swfReturn->padding = 0;
        swfReturn->value.floatValue = value;
    }

    HOOKCRASHERS_API void HookCrashers_SetReturnString(HC_SWFReturn* swfReturn, uint16_t stringId) {
        if (!swfReturn) return;
        swfReturn->status = 0;
        swfReturn->type = HC_SWFReturn::Type::String;
        swfReturn->padding = 0;
        swfReturn->value.rawValue = 0;
        swfReturn->value.stringId = stringId;
    }

    HOOKCRASHERS_API void HookCrashers_SetReturnFailure(HC_SWFReturn* swfReturn) {
        if (!swfReturn) return;
        swfReturn->status = 1;
        swfReturn->type = HC_SWFReturn::Type::None;
        swfReturn->padding = 0;
        swfReturn->value.rawValue = 0;
    }

    // --- Memory Patcher ---
    HOOKCRASHERS_API bool HookCrashers_PatchBytes(uintptr_t address, const std::vector<uint8_t>& newBytes) {
        return HookCrashers::Util::MemoryPatcher::PatchBytes(address, newBytes);
    }

    // --- SWFArgumentReader ---
    HOOKCRASHERS_API int32_t HookCrashers_Arg_GetInteger(const HC_SWFArgument* arg, int32_t defaultVal) {
        if (!arg) return defaultVal;
        if (arg->type == HC_SWFArgument::Type::Integer) return arg->value.intValue;
        if (arg->type == HC_SWFArgument::Type::Boolean) return arg->value.boolValue;
        return defaultVal;
    }

    HOOKCRASHERS_API bool HookCrashers_Arg_GetBoolean(const HC_SWFArgument* arg, bool defaultVal) {
        if (!arg) return defaultVal;
        if (arg->type == HC_SWFArgument::Type::Boolean) return arg->value.boolValue != 0;
        if (arg->type == HC_SWFArgument::Type::Integer) return arg->value.intValue != 0;
        return defaultVal;
    }

    HOOKCRASHERS_API float HookCrashers_Arg_GetFloat(const HC_SWFArgument* arg, float defaultVal) {
        if (!arg) return defaultVal;
        if (arg->type == HC_SWFArgument::Type::Float) return arg->value.floatValue;
        if (arg->type == HC_SWFArgument::Type::Integer) return static_cast<float>(arg->value.intValue);
        return defaultVal;
    }

    HOOKCRASHERS_API uint16_t HookCrashers_Arg_GetStringId(const HC_SWFArgument* arg, uint16_t defaultVal) {
        if (!arg || arg->type != HC_SWFArgument::Type::String) return defaultVal;
        return arg->value.stringId;
    }
}
