#pragma once

#include "../../stdafx.h"
#include "../Data/SWFReturn.h"

namespace HookCrashers {
    namespace SWF {
        namespace Helpers {

            class SWFReturnHelper {
            public:
                static Data::SWFReturn* AsStructured(uint32_t* swfReturnValuePtr);

                static void SetBooleanSuccess(uint32_t* swfReturnValuePtr, bool value);
                static void SetIntegerSuccess(uint32_t* swfReturnValuePtr, int32_t value);
                static void SetFloatSuccess(uint32_t* swfReturnValuePtr, float value);
                static void SetStringSuccess(uint32_t* swfReturnValuePtr, uint16_t stringId);
                static void SetFailure(uint32_t* swfReturnValuePtr);
                static void SetVoidSuccess(uint32_t* swfReturnValuePtr);
            };

        }
    }
}