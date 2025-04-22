#pragma once

#include "../stdafx.h"

namespace HookCrashers {
	namespace Core {

		// Setup the hook for the main SWF function dispatcher (FUN_00bfa070)
		bool SetupCallSWFFunctionHook(uintptr_t moduleBase);

		// Get the address of the original CallSWFFunction (needed by SwfDispatcher)
		uintptr_t GetOriginalCallSWFFunctionAddress();
	}
}