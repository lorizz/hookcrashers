#pragma once

#include "../stdafx.h"

namespace HookCrashers {
	namespace Core {

		// Setup the hook for the game's function that triggers registration of all (or many) SWF functions
		bool SetupRegisterAllSWFFunctionsHook(uintptr_t moduleBase);
	}
}