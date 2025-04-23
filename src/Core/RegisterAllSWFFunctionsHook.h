#pragma once

#include "../stdafx.h"

namespace HookCrashers {
	namespace Core {
		bool SetupRegisterAllSWFFunctionsHook(uintptr_t moduleBase);
	}
}