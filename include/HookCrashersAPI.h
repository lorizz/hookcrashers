#pragma once

#include "HookCrashers/Public/Types.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

#ifdef HOOKCRASHERS_EXPORTS
#define HOOKCRASHERS_API __declspec(dllexport)
#else
#define HOOKCRASHERS_API __declspec(dllimport)
#endif

#define HOOKCRASHERS_API_VERSION 1.1f

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

    // --- String Management ---
    HOOKCRASHERS_API uint16_t HookCrashers_AddString(const char* stringToAdd);
    HOOKCRASHERS_API size_t HookCrashers_GetString(uint16_t stringId, char* buffer, size_t bufferSize);

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

            static uint16_t AddString(const std::string& str) {
                return HookCrashers_AddString(str.c_str());
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
    }
}
#endif