#pragma once

#include "../../stdafx.h"
#include "../Data/SWFReturn.h" // Use Data struct

namespace HookCrashers {
    namespace SWF {
        namespace Helpers {

            // Helper class to simplify setting return values via the raw uint32_t* pointer
            // This is useful because the game passes the return value destination as uint32_t*
            class SWFReturnHelper {
            public:
                // Cast the raw game pointer to our structure for easier access (use with caution)
                static Data::SWFReturn* AsStructured(uint32_t* swfReturnValuePtr);

                // Set return values directly using the raw pointer
                static void SetBooleanSuccess(uint32_t* swfReturnValuePtr, bool value);
                static void SetIntegerSuccess(uint32_t* swfReturnValuePtr, int32_t value);
                static void SetFloatSuccess(uint32_t* swfReturnValuePtr, float value);
                static void SetStringSuccess(uint32_t* swfReturnValuePtr, uint16_t stringId); // Return the ID
                static void SetFailure(uint32_t* swfReturnValuePtr);
                static void SetVoidSuccess(uint32_t* swfReturnValuePtr); // For functions that return nothing on success
            };

        } // namespace Helpers
    } // namespace SWF
} // namespace HookCrashers