#pragma once
#include <cstdint>
#include "../logger.h"
#include "../structs/SWFArgument.h"
#include "../structs/SWFReturn.h"
#include "../structs/SWFFunctionIDs.h"
#include "../hooks/HookCrashers.h"

namespace CustomSWFFunctions {
    typedef std::function<void(int paramCount, SWFArgument** swfArgs, SWFReturn* swfReturn)> CustomSWFFunction;

    void HelloWorld(int paramCount, SWFArgument** swfArgs, SWFReturn* swfReturn);

    bool RegisterCustomFunction(SWFFunctionID functionId, CustomSWFFunction function);

    bool HandleCustomCall(uint16_t functionId, void* thisPtr, int swfContext,
        int paramCount, SWFArgument** swfArgs, SWFReturn* swfReturn, uint32_t callbackPtr);

    bool IsCustomFunction(uint16_t id);

    void Initialize();

    // Get a list of all registered custom function IDs
    std::vector<uint16_t> GetRegisteredFunctionIds();
}