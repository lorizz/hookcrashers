#include "../src/stdafx.h"
#include "HookCrashersAPI.h"
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
#include "../src/Core/DecryptSaveFileHook.h"
#include "../src/Util/SteamHelper.h"

#include <functional>
#include <unordered_map>
#include "HookCrashers/Public/NativeCaller.h"
#include "HookCrashers/Public/NativeFunctions.h"
#include "../src/Core/BlowfishDecryptHook.h"

namespace HookCrashers {
    using CustomSWFFunction = std::function<void(int, HC_SWFArgument**, HC_SWFReturn*)>;
    using OverrideFunction = std::function<void(void*, int, uint32_t, int, HC_SWFArgument**, HC_SWFReturn*, uint32_t)>;
    using SaveDecryptedFunction = std::function<void(const uint8_t*, int)>;

    static std::unordered_map<uint16_t, CustomSWFFunction*> s_customCallbacks;
    static std::unordered_map<uint16_t, OverrideFunction*> s_overrideCallbacks;
    static SaveDecryptedFunction* s_saveDecryptedCallback = nullptr;
}

void CustomCallbackTrampoline(int p, HC_SWFArgument** a, HC_SWFReturn* r, uint16_t functionId) {
    auto it = HookCrashers::s_customCallbacks.find(functionId);
    if (it != HookCrashers::s_customCallbacks.end() && it->second) {
        (*it->second)(p, a, r);
    }
}

void OverrideCallbackTrampoline(void* t, int s, uint32_t f, int p, HC_SWFArgument** a, HC_SWFReturn* r, uint32_t c) {
    auto it = HookCrashers::s_overrideCallbacks.find(f & 0xFFFF);
    if (it != HookCrashers::s_overrideCallbacks.end() && it->second) {
        (*it->second)(t, s, f, p, a, r, c);
    }
}

void SaveDecryptedCallbackTrampoline(const uint8_t* buffer, int size) {
    if (HookCrashers::s_saveDecryptedCallback) {
        (*HookCrashers::s_saveDecryptedCallback)(buffer, size);
    }
}

extern "C" {
    HOOKCRASHERS_API bool __stdcall HookCrashers_Initialize(uintptr_t moduleBase) { return HookCrashers::Core::HookManager::Initialize(moduleBase); }
    HOOKCRASHERS_API float __stdcall HookCrashers_GetVersion() { return HookCrashers::Core::HookManager::GetVersion(); }
    HOOKCRASHERS_API bool __stdcall HookCrashers_IsInitialized() { return HookCrashers::Core::HookManager::IsInitialized(); }
    HOOKCRASHERS_API uintptr_t* __stdcall HookCrashers_GetGameManagerPtr() { return HookCrashers::Core::g_pGameManagerPtr; }
    HOOKCRASHERS_API uintptr_t __stdcall HookCrashers_GetModuleBase() { return g_moduleBase; }
    HOOKCRASHERS_API uint16_t __stdcall HookCrashers_AddString(const char* stringToAdd) { return HookCrashers::Core::AddCustomString(stringToAdd); }
    HOOKCRASHERS_API size_t __stdcall HookCrashers_GetString(uint16_t stringId, char* buffer, size_t bufferSize) {
        std::string str = HookCrashers::Util::StringCache::GetStringCopyById(stringId);
        if (buffer == nullptr) return str.length();
        if (bufferSize > str.length()) {
            strcpy_s(buffer, bufferSize, str.c_str());
            return str.length();
        }
        return 0;
    }
    HOOKCRASHERS_API uint32_t __stdcall HookCrashers_IsFeatureEnabled(uint16_t featureId) { return HookCrashers::Core::GetIsFeatureEnabled(featureId); }
    HOOKCRASHERS_API void* __stdcall HookCrashers_GetPlayerObject(int playerIndex) { return HookCrashers::Core::GetPlayerObject(playerIndex); }
    HOOKCRASHERS_API char __stdcall HookCrashers_GetPlayerState(void* playerObject) { return HookCrashers::Core::GetPlayerState(playerObject); }
    HOOKCRASHERS_API char __stdcall HookCrashers_GetPlayerActiveState(void* playerObject) { return HookCrashers::Core::GetPlayerActiveState(playerObject); }
    HOOKCRASHERS_API uint64_t __stdcall HookCrashers_GetPlayerPosition(void* playerObject) { return HookCrashers::Core::GetPlayerPosition(playerObject); }
    HOOKCRASHERS_API int __stdcall HookCrashers_GetPlayerSelectedCharacterType(void* playerObject) { return HookCrashers::Core::GetPlayerSelectedCharacterType(playerObject); }
    HOOKCRASHERS_API bool __stdcall HookCrashers_IsOnlineMode() { return HookCrashers::Core::IsOnlineMode(); }
    HOOKCRASHERS_API void __stdcall HookCrashers_CallOriginal(void* t, int s, uint32_t f, int p, HC_SWFArgument** a, uint32_t* r, uint32_t c) { HookCrashers::SWF::Dispatcher::CallOriginal(t, s, f, p, a, r, c); }
    HOOKCRASHERS_API void __stdcall HookCrashers_LogDebug(const char* message) { if (message) HookCrashers::Util::Logger::Instance().Get()->debug(message); }
    HOOKCRASHERS_API void __stdcall HookCrashers_LogInfo(const char* message) { if (message) HookCrashers::Util::Logger::Instance().Get()->info(message); }
    HOOKCRASHERS_API void __stdcall HookCrashers_LogWarn(const char* message) { if (message) HookCrashers::Util::Logger::Instance().Get()->warn(message); }
    HOOKCRASHERS_API void __stdcall HookCrashers_LogError(const char* message) { if (message) HookCrashers::Util::Logger::Instance().Get()->error(message); }

    HOOKCRASHERS_API bool __stdcall HookCrashers_RegisterCustomSWF_CPP(uint16_t id, const char* name, void* callback) {
        auto* func_ptr = static_cast<HookCrashers::CustomSWFFunction*>(callback);
        if (!func_ptr) return false;
        if (HookCrashers::s_customCallbacks.count(id)) {
            delete HookCrashers::s_customCallbacks[id];
        }
        HookCrashers::s_customCallbacks[id] = func_ptr;
        auto internalCallback = [id](int p, HC_SWFArgument** a, HC_SWFReturn* r) {
            CustomCallbackTrampoline(p, a, r, id);
            };
        return HookCrashers::SWF::Custom::Register(id, name, internalCallback);
    }

    HOOKCRASHERS_API bool __stdcall HookCrashers_RegisterOverride_CPP(uint16_t id, void* callback) {
        auto* func_ptr = static_cast<HookCrashers::OverrideFunction*>(callback);
        if (!func_ptr) return false;
        if (HookCrashers::s_overrideCallbacks.count(id)) {
            delete HookCrashers::s_overrideCallbacks[id];
        }
        HookCrashers::s_overrideCallbacks[id] = func_ptr;
        return HookCrashers::SWF::Override::Register(id, OverrideCallbackTrampoline);
    }

    HOOKCRASHERS_API void __stdcall HookCrashers_SetReturnBool(HC_SWFReturn* r, bool v) { if (r) { r->status = 0; r->type = HC_SWFReturn::Type::Boolean; r->padding = 0; r->value.rawValue = 0; r->value.boolValue = static_cast<uint8_t>(v ? 1 : 0); } }
    HOOKCRASHERS_API void __stdcall HookCrashers_SetReturnInt(HC_SWFReturn* r, int32_t v) { if (r) { r->status = 0; r->type = HC_SWFReturn::Type::Integer; r->padding = 0; r->value.intValue = v; } }
    HOOKCRASHERS_API void __stdcall HookCrashers_SetReturnFloat(HC_SWFReturn* r, float v) { if (r) { r->status = 0; r->type = HC_SWFReturn::Type::Float; r->padding = 0; r->value.floatValue = v; } }
    HOOKCRASHERS_API void __stdcall HookCrashers_SetReturnString(HC_SWFReturn* r, uint16_t v) { if (r) { r->status = 0; r->type = HC_SWFReturn::Type::String; r->padding = 0; r->value.rawValue = 0; r->value.stringId = v; } }
    HOOKCRASHERS_API void __stdcall HookCrashers_SetReturnFailure(HC_SWFReturn* r) { if (r) { r->status = 1; r->type = HC_SWFReturn::Type::None; r->padding = 0; r->value.rawValue = 0; } }

    HOOKCRASHERS_API bool __stdcall HookCrashers_PatchBytes(uintptr_t address, const uint8_t* data, size_t size) {
        if (!data || size == 0) return false;
        return HookCrashers::Util::MemoryPatcher::PatchBytes(address, std::vector<uint8_t>(data, data + size));
    }

    HOOKCRASHERS_API int32_t __stdcall HookCrashers_Arg_GetInteger(const HC_SWFArgument* a, int32_t d) { if (!a) return d; if (a->type == HC_SWFArgument::Type::Integer) return a->value.intValue; if (a->type == HC_SWFArgument::Type::Boolean) return a->value.boolValue; return d; }
    HOOKCRASHERS_API bool __stdcall HookCrashers_Arg_GetBoolean(const HC_SWFArgument* a, bool d) { if (!a) return d; if (a->type == HC_SWFArgument::Type::Boolean) return a->value.boolValue != 0; if (a->type == HC_SWFArgument::Type::Integer) return a->value.intValue != 0; return d; }
    HOOKCRASHERS_API float __stdcall HookCrashers_Arg_GetFloat(const HC_SWFArgument* a, float d) { if (!a) return d; if (a->type == HC_SWFArgument::Type::Float) return a->value.floatValue; if (a->type == HC_SWFArgument::Type::Integer) return static_cast<float>(a->value.intValue); return d; }
    HOOKCRASHERS_API uint16_t __stdcall HookCrashers_Arg_GetStringId(const HC_SWFArgument* a, uint16_t d) { if (!a || a->type != HC_SWFArgument::Type::String) return d; return a->value.stringId; }

    HOOKCRASHERS_API void __stdcall HookCrashers_FindCastleCrashersSavePath_CPP(char* buffer, size_t bufferSize, bool* success) {
        if (!buffer || !success) return;
        auto result = HookCrashers::Util::FindCastleCrashersSavePath();
        *success = result.first;
        if (result.first) {
            strcpy_s(buffer, bufferSize, result.second.c_str());
        }
        else {
            buffer[0] = '\0';
        }
    }

    HOOKCRASHERS_API const std::vector<uint8_t>& __stdcall HookCrashers_GetCapturedSaveData() {
        return HookCrashers::Core::GetCapturedSaveData();
    }
}