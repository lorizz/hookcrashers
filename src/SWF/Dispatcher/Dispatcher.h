#pragma once

#include "../../stdafx.h"
#include "../Data/SWFArgument.h"
#include "../Data/SWFReturn.h"

namespace HookCrashers {
	namespace SWF {
		namespace Dispatcher {
            bool Dispatch(
                void* gameThisPtr,      // The 'this' pointer from the game's call context
                int swfContext,         // param_1 in original hook
                uint32_t functionIdRaw, // param_2 in original hook (contains ID and maybe flags)
                int paramCount,         // param_3 in original hook
                Data::SWFArgument** swfArgs, // param_4: Array of argument pointers
                uint32_t* swfReturnRaw, // param_5: Pointer to where return value should be stored
                uint32_t callbackPtr    // param_6: Callback pointer/ID
                // We need the original function pointer to call it if not handled here
                // Pass it during initialization or make it accessible globally/statically?
            );

            void Initialize(uintptr_t originalCallSWFFuncAddr);
		}
	}
}