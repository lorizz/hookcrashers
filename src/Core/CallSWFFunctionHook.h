#pragma once

#include "../stdafx.h"

namespace HookCrashers {
	namespace Core {
		bool SetupCallSWFFunctionHook(uintptr_t moduleBase);
		uintptr_t GetOriginalCallSWFFunctionAddress();
	}
}