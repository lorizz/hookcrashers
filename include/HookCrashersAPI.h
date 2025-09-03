#pragma once

#include <cstdint>
#include <vector>
#include <string>

#include "HookCrashers/Public/Types.h"
#include "HookCrashers/Public/Globals.h"
using HC_SWFArgument = HookCrashers::SWF::Data::SWFArgument;
using HC_SWFReturn = HookCrashers::SWF::Data::SWFReturn;

#ifdef HOOKCRASHERS_EXPORTS
#define HOOKCRASHERS_API __declspec(dllexport)
#else
#define HOOKCRASHERS_API __declspec(dllimport)
#endif

extern "C" {
    HOOKCRASHERS_API bool __stdcall HookCrashers_Initialize(uintptr_t moduleBase);
    HOOKCRASHERS_API float __stdcall HookCrashers_GetVersion();
    HOOKCRASHERS_API bool __stdcall HookCrashers_IsInitialized();
    HOOKCRASHERS_API uintptr_t* __stdcall HookCrashers_GetGameManagerPtr();
    HOOKCRASHERS_API uintptr_t __stdcall HookCrashers_GetModuleBase();

    HOOKCRASHERS_API uint16_t __stdcall HookCrashers_AddString(const char* stringToAdd);
    HOOKCRASHERS_API size_t __stdcall HookCrashers_GetString(uint16_t stringId, char* buffer, size_t bufferSize);

    HOOKCRASHERS_API uint32_t __stdcall HookCrashers_IsFeatureEnabled(uint16_t featureId);
    HOOKCRASHERS_API void* __stdcall HookCrashers_GetPlayerObject(int playerIndex);
    HOOKCRASHERS_API char __stdcall HookCrashers_GetPlayerState(void* playerObject);
    HOOKCRASHERS_API char __stdcall HookCrashers_GetPlayerActiveState(void* playerObject);
    HOOKCRASHERS_API uint64_t __stdcall HookCrashers_GetPlayerPosition(void* playerObject);
    HOOKCRASHERS_API int __stdcall HookCrashers_GetPlayerSelectedCharacterType(void* playerObject);
    HOOKCRASHERS_API bool __stdcall HookCrashers_IsOnlineMode();

    HOOKCRASHERS_API bool __stdcall HookCrashers_RegisterCustomSWF_CPP(uint16_t functionId, const char* functionName, void* callback);
    HOOKCRASHERS_API bool __stdcall HookCrashers_RegisterOverride_CPP(uint16_t functionId, void* callback);
    HOOKCRASHERS_API void __stdcall HookCrashers_CallOriginal(void* thisPtr, int swfContext, uint32_t functionIdRaw, int paramCount, HC_SWFArgument** swfArgs, uint32_t* swfReturnRaw, uint32_t callbackPtr);

    HOOKCRASHERS_API void __stdcall HookCrashers_LogInfo(const char* message);
    HOOKCRASHERS_API void __stdcall HookCrashers_LogWarn(const char* message);
    HOOKCRASHERS_API void __stdcall HookCrashers_LogError(const char* message);

    HOOKCRASHERS_API void __stdcall HookCrashers_SetReturnBool(HC_SWFReturn* swfReturn, bool value);
    HOOKCRASHERS_API void __stdcall HookCrashers_SetReturnInt(HC_SWFReturn* swfReturn, int32_t value);
    HOOKCRASHERS_API void __stdcall HookCrashers_SetReturnFloat(HC_SWFReturn* swfReturn, float value);
    HOOKCRASHERS_API void __stdcall HookCrashers_SetReturnString(HC_SWFReturn* swfReturn, uint16_t stringId);
    HOOKCRASHERS_API void __stdcall HookCrashers_SetReturnFailure(HC_SWFReturn* swfReturn);

    HOOKCRASHERS_API int32_t __stdcall HookCrashers_Arg_GetInteger(const HC_SWFArgument* arg, int32_t defaultVal);
    HOOKCRASHERS_API bool __stdcall HookCrashers_Arg_GetBoolean(const HC_SWFArgument* arg, bool defaultVal);
    HOOKCRASHERS_API float __stdcall HookCrashers_Arg_GetFloat(const HC_SWFArgument* arg, float defaultVal);
    HOOKCRASHERS_API uint16_t __stdcall HookCrashers_Arg_GetStringId(const HC_SWFArgument* arg, uint16_t defaultVal);

    HOOKCRASHERS_API bool __stdcall HookCrashers_PatchBytes(uintptr_t address, const uint8_t* data, size_t size);

    HOOKCRASHERS_API void __stdcall HookCrashers_FindCastleCrashersSavePath_CPP(char* buffer, size_t bufferSize, bool* success);
    HOOKCRASHERS_API const std::vector<uint8_t>& __stdcall HookCrashers_GetCapturedSaveData();
}