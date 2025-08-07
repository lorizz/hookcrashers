#pragma once

#include "../../stdafx.h"
#include "../../../include/HookCrashers/Public/Types.h"

// For convenience
using HC_SWFArgument = HookCrashers::SWF::Data::SWFArgument;

namespace HookCrashers {
    namespace SWF {
        namespace Dispatcher {
            bool Dispatch(
                void* gameThisPtr,
                int swfContext,
                uint32_t functionIdRaw,
                int paramCount,
                // FIXED: Use the correct public type name
                HC_SWFArgument** swfArgs,
                uint32_t* swfReturnRaw,
                uint32_t callbackPtr
            );

            void Initialize(uintptr_t originalCallSWFFuncAddr);
            void CallOriginal(void* thisPtr, int swfContext, uint32_t functionIdRaw, int paramCount,
                HC_SWFArgument** swfArgs, uint32_t* swfReturnRaw, uint32_t callbackPtr);
        }
    }
}