#pragma once
#include <functional>
#include <unordered_map>
#include <stdexcept>
#include "../structs/SWFArgument.h"
#include "../structs/SWFReturn.h"
#include "../structs/SWFFunctionIDs.h"

namespace HookCrashers {
    using HookFunction = std::function<void(void* thisPtr, int swfContext, uint32_t functionId, int paramCount,
                SWFArgument** swfArgs, SWFReturn* swfReturn, uint32_t callbackPtr)>;

    bool Override(SWFFunctionID functionId, HookFunction hookFunction);

    bool Override(uint16_t functionId, HookFunction hookFunction);

    HookFunction GetHookFunction(uint16_t functionId);

    void Initialize(uintptr_t moduleBase);
}