#pragma once

// REMOVED: #include "../../stdafx.h" // Headers should be self-contained
#include "../../../include/HookCrashers/Public/Types.h"

// For convenience
using HC_SWFReturn = HookCrashers::SWF::Data::SWFReturn;

namespace HookCrashers {
    namespace SWF {
        namespace Helpers {
            // This class now operates on the public SWFReturn struct
            class SWFReturnHelper {
            public:
                // ADD THIS FUNCTION
                // Safely casts a raw pointer from the game to our structured type.
                static HC_SWFReturn* AsStructured(uint32_t* swfReturnRaw);

                // FIXED: Use the alias for consistency
                static void SetBooleanSuccess(HC_SWFReturn* swfReturn, bool value);
                static void SetIntegerSuccess(HC_SWFReturn* swfReturn, int32_t value);
                static void SetFloatSuccess(HC_SWFReturn* swfReturn, float value);
                static void SetStringSuccess(HC_SWFReturn* swfReturn, uint16_t stringId);
                static void SetFailure(HC_SWFReturn* swfReturn);
            };
        }
    }
}