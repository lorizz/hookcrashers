#pragma once

#include <string>
#include <vector>
#include <functional>
#include <utility>
#include <cstdint>

#include "HookCrashers/Public/Types.h"
#include "HookCrashers/Public/Globals.h"

using HC_SWFArgument = HookCrashers::SWF::Data::SWFArgument;
using HC_SWFReturn = HookCrashers::SWF::Data::SWFReturn;

namespace HookCrashers
{
    using CustomSWFFunction = std::function<void(int argCount, HC_SWFArgument** args, HC_SWFReturn* ret)>;
    using OverrideFunction = std::function<void(void* thisPtr, int swfCtx, uint32_t funcId, int argCount, HC_SWFArgument** args, HC_SWFReturn* ret, uint32_t callbackPtr)>;

    bool Initialize(uintptr_t moduleBase = 0);
    bool IsInitialized();
    float GetVersion();
    uintptr_t GetModuleBase();

    void LogInfo(const std::string& message);
    void LogWarn(const std::string& message);
    void LogError(const std::string& message);

    uint16_t AddString(const std::string& text);
    std::string GetString(uint16_t stringId);

    uintptr_t* GetGameManagerPtr();
    uint32_t IsFeatureEnabled(uint16_t featureId);
    void* GetPlayerObject(int playerIndex);
    char GetPlayerState(void* playerObject);
    char GetPlayerActiveState(void* playerObject);
    uint64_t GetPlayerPosition(void* playerObject);
    int GetPlayerSelectedCharacterType(void* playerObject);
    bool IsOnlineMode();

    bool RegisterCustomSWF(uint16_t functionId, const std::string& functionName, CustomSWFFunction callback);
    bool RegisterOverride(uint16_t functionId, OverrideFunction callback);
    void CallOriginal(void* thisPtr, int swfCtx, uint32_t funcId, int argCount, HC_SWFArgument** args, uint32_t* rawReturn, uint32_t callbackPtr);

    bool PatchBytes(uintptr_t address, const std::vector<uint8_t>& bytes);

    std::pair<bool, std::string> FindCastleCrashersSavePath();
    const std::vector<uint8_t>& GetCapturedSaveData();

    namespace SWF
    {
        class ArgsReader
        {
        private:
            HC_SWFArgument** m_args;
            int m_count;
        public:
            ArgsReader(int count, HC_SWFArgument** args);

            int Count() const;
            int32_t GetInt(int index, int32_t defaultVal = 0) const;
            bool GetBool(int index, bool defaultVal = false) const;
            float GetFloat(int index, float defaultVal = 0.0f) const;
            uint16_t GetStringId(int index, uint16_t defaultVal = 0) const;
            std::string GetString(int index, const std::string& defaultVal = "") const;
            std::string Dump(int index) const;
        };

        class ReturnValue
        {
        private:
            HC_SWFReturn* m_return;
        public:
            ReturnValue(HC_SWFReturn* ret);

            void SetInt(int32_t value);
            void SetBool(bool value);
            void SetFloat(float value);
            void SetString(const std::string& value);
            void SetStringId(uint16_t stringId);
            void SetFailure();
        };
    }
}

#ifndef HOOKCRASHERS_EXPORTS

extern "C" {
    __declspec(dllimport) bool __stdcall HookCrashers_Initialize(uintptr_t moduleBase);
    __declspec(dllimport) float __stdcall HookCrashers_GetVersion();
    __declspec(dllimport) bool __stdcall HookCrashers_IsInitialized();
    __declspec(dllimport) uintptr_t* __stdcall HookCrashers_GetGameManagerPtr();
    __declspec(dllimport) uintptr_t __stdcall HookCrashers_GetModuleBase();
    __declspec(dllimport) uint16_t __stdcall HookCrashers_AddString(const char* stringToAdd);
    __declspec(dllimport) size_t __stdcall HookCrashers_GetString(uint16_t stringId, char* buffer, size_t bufferSize);
    __declspec(dllimport) uint32_t __stdcall HookCrashers_IsFeatureEnabled(uint16_t featureId);
    __declspec(dllimport) void* __stdcall HookCrashers_GetPlayerObject(int playerIndex);
    __declspec(dllimport) char __stdcall HookCrashers_GetPlayerState(void* playerObject);
    __declspec(dllimport) char __stdcall HookCrashers_GetPlayerActiveState(void* playerObject);
    __declspec(dllimport) uint64_t __stdcall HookCrashers_GetPlayerPosition(void* playerObject);
    __declspec(dllimport) int __stdcall HookCrashers_GetPlayerSelectedCharacterType(void* playerObject);
    __declspec(dllimport) bool __stdcall HookCrashers_IsOnlineMode();
    __declspec(dllimport) void __stdcall HookCrashers_CallOriginal(void* thisPtr, int swfContext, uint32_t functionIdRaw, int paramCount, HC_SWFArgument** swfArgs, uint32_t* swfReturnRaw, uint32_t callbackPtr);
    __declspec(dllimport) void __stdcall HookCrashers_LogInfo(const char* message);
    __declspec(dllimport) void __stdcall HookCrashers_LogWarn(const char* message);
    __declspec(dllimport) void __stdcall HookCrashers_LogError(const char* message);
    __declspec(dllimport) void __stdcall HookCrashers_SetReturnBool(HC_SWFReturn* swfReturn, bool value);
    __declspec(dllimport) void __stdcall HookCrashers_SetReturnInt(HC_SWFReturn* swfReturn, int32_t value);
    __declspec(dllimport) void __stdcall HookCrashers_SetReturnFloat(HC_SWFReturn* swfReturn, float value);
    __declspec(dllimport) void __stdcall HookCrashers_SetReturnString(HC_SWFReturn* swfReturn, uint16_t stringId);
    __declspec(dllimport) void __stdcall HookCrashers_SetReturnFailure(HC_SWFReturn* swfReturn);
    __declspec(dllimport) bool __stdcall HookCrashers_PatchBytes(uintptr_t address, const uint8_t* data, size_t size);
    __declspec(dllimport) int32_t __stdcall HookCrashers_Arg_GetInteger(const HC_SWFArgument* arg, int32_t defaultVal);
    __declspec(dllimport) bool __stdcall HookCrashers_Arg_GetBoolean(const HC_SWFArgument* arg, bool defaultVal);
    __declspec(dllimport) float __stdcall HookCrashers_Arg_GetFloat(const HC_SWFArgument* arg, float defaultVal);
    __declspec(dllimport) uint16_t __stdcall HookCrashers_Arg_GetStringId(const HC_SWFArgument* arg, uint16_t defaultVal);
    __declspec(dllimport) void __stdcall HookCrashers_FindCastleCrashersSavePath_CPP(char* buffer, size_t bufferSize, bool* success);
    __declspec(dllimport) bool __stdcall HookCrashers_RegisterCustomSWF_CPP(uint16_t id, const char* name, void* callback);
    __declspec(dllimport) bool __stdcall HookCrashers_RegisterOverride_CPP(uint16_t id, void* callback);
    __declspec(dllimport) const std::vector<uint8_t>& __stdcall HookCrashers_GetCapturedSaveData();
}

namespace HookCrashers
{
    inline bool Initialize(uintptr_t moduleBase) { return HookCrashers_Initialize(moduleBase); }
    inline bool IsInitialized() { return HookCrashers_IsInitialized(); }
    inline float GetVersion() { return HookCrashers_GetVersion(); }
    inline uintptr_t GetModuleBase() { return HookCrashers_GetModuleBase(); }
    inline void LogInfo(const std::string& message) { HookCrashers_LogInfo(message.c_str()); }
    inline void LogWarn(const std::string& message) { HookCrashers_LogWarn(message.c_str()); }
    inline void LogError(const std::string& message) { HookCrashers_LogError(message.c_str()); }
    inline uint16_t AddString(const std::string& text) { return HookCrashers_AddString(text.c_str()); }
    inline std::string GetString(uint16_t stringId) {
        size_t requiredSize = HookCrashers_GetString(stringId, nullptr, 0);
        if (requiredSize == 0) return "";
        std::string result(requiredSize, '\0');
        HookCrashers_GetString(stringId, &result[0], result.size() + 1);
        return result;
    }
    inline uintptr_t* GetGameManagerPtr() { return HookCrashers_GetGameManagerPtr(); }
    inline uint32_t IsFeatureEnabled(uint16_t featureId) { return HookCrashers_IsFeatureEnabled(featureId); }
    inline void* GetPlayerObject(int playerIndex) { return HookCrashers_GetPlayerObject(playerIndex); }
    inline char GetPlayerState(void* playerObject) { return HookCrashers_GetPlayerState(playerObject); }
    inline char GetPlayerActiveState(void* playerObject) { return HookCrashers_GetPlayerActiveState(playerObject); }
    inline uint64_t GetPlayerPosition(void* playerObject) { return HookCrashers_GetPlayerPosition(playerObject); }
    inline int GetPlayerSelectedCharacterType(void* playerObject) { return HookCrashers_GetPlayerSelectedCharacterType(playerObject); }
    inline bool IsOnlineMode() { return HookCrashers_IsOnlineMode(); }
    inline void CallOriginal(void* t, int s, uint32_t f, int p, HC_SWFArgument** a, uint32_t* r, uint32_t c) { HookCrashers_CallOriginal(t, s, f, p, a, r, c); }
    inline bool PatchBytes(uintptr_t address, const std::vector<uint8_t>& bytes) { return HookCrashers_PatchBytes(address, bytes.data(), bytes.size()); }
    inline std::pair<bool, std::string> FindCastleCrashersSavePath() {
        char pathBuffer[512];
        bool success = false;
        HookCrashers_FindCastleCrashersSavePath_CPP(pathBuffer, sizeof(pathBuffer), &success);
        return { success, std::string(pathBuffer) };
    }
    inline bool RegisterCustomSWF(uint16_t id, const std::string& name, CustomSWFFunction callback) {
        auto* cb_heap = new CustomSWFFunction(std::move(callback));
        return HookCrashers_RegisterCustomSWF_CPP(id, name.c_str(), cb_heap);
    }
    inline bool RegisterOverride(uint16_t id, OverrideFunction callback) {
        auto* cb_heap = new OverrideFunction(std::move(callback));
        return HookCrashers_RegisterOverride_CPP(id, cb_heap);
    }

    inline const std::vector<uint8_t>& GetCapturedSaveData() {
        return HookCrashers_GetCapturedSaveData();
    }

    namespace SWF {
        inline ArgsReader::ArgsReader(int count, HC_SWFArgument** args) : m_args(args), m_count(count) {}
        inline int ArgsReader::Count() const { return m_count; }
        inline int32_t ArgsReader::GetInt(int index, int32_t defaultVal) const {
            if (index < 0 || index >= m_count) return defaultVal;
            return HookCrashers_Arg_GetInteger(m_args[index], defaultVal);
        }
        inline bool ArgsReader::GetBool(int index, bool defaultVal) const {
            if (index < 0 || index >= m_count) return defaultVal;
            return HookCrashers_Arg_GetBoolean(m_args[index], defaultVal);
        }
        inline float ArgsReader::GetFloat(int index, float defaultVal) const {
            if (index < 0 || index >= m_count) return defaultVal;
            return HookCrashers_Arg_GetFloat(m_args[index], defaultVal);
        }
        inline uint16_t ArgsReader::GetStringId(int index, uint16_t defaultVal) const {
            if (index < 0 || index >= m_count) return defaultVal;
            return HookCrashers_Arg_GetStringId(m_args[index], defaultVal);
        }
        inline std::string ArgsReader::GetString(int index, const std::string& defaultVal) const {
            uint16_t id = GetStringId(index, 0);
            if (id == 0) return defaultVal;
            return HookCrashers::GetString(id);
        }
        inline std::string ArgsReader::Dump(int index) const {
            if (index < 0 || index >= m_count || !m_args[index]) return "[Invalid Arg]";
            const auto* arg = m_args[index];
            switch (arg->type) {
            case HC_SWFArgument::Type::Boolean: return GetBool(index) ? "true" : "false";
            case HC_SWFArgument::Type::Integer: return std::to_string(GetInt(index));
            case HC_SWFArgument::Type::Float: return std::to_string(GetFloat(index));
            case HC_SWFArgument::Type::String: return "\"" + GetString(index, "[Invalid ID]") + "\"";
            default: return "[Unknown Type]";
            }
        }

        inline ReturnValue::ReturnValue(HC_SWFReturn* ret) : m_return(ret) {}
        inline void ReturnValue::SetInt(int32_t value) { HookCrashers_SetReturnInt(m_return, value); }
        inline void ReturnValue::SetBool(bool value) { HookCrashers_SetReturnBool(m_return, value); }
        inline void ReturnValue::SetFloat(float value) { HookCrashers_SetReturnFloat(m_return, value); }
        inline void ReturnValue::SetString(const std::string& value) {
            uint16_t id = HookCrashers::AddString(value);
            HookCrashers_SetReturnString(m_return, id);
        }
        inline void ReturnValue::SetStringId(uint16_t stringId) { HookCrashers_SetReturnString(m_return, stringId); }
        inline void ReturnValue::SetFailure() { HookCrashers_SetReturnFailure(m_return); }
    }
}
#endif