#pragma once

#include "../../stdafx.h"
#include "../Data/SWFArgument.h"
#include "../Data/SWFReturn.h"

namespace HookCrashers {
	namespace SWF {
		namespace Dispatcher {
            bool Dispatch(
                void* gameThisPtr,
                int swfContext,
                uint32_t functionIdRaw,
                int paramCount,
                Data::SWFArgument** swfArgs,
                uint32_t* swfReturnRaw,
                uint32_t callbackPtr
            );

            void Initialize(uintptr_t originalCallSWFFuncAddr);
		}
	}
}