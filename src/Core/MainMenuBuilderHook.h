#pragma once

#include "../stdafx.h"
#include <cstdint>

namespace HookCrashers {
	namespace Core {
		extern void* g_pModLoaderMenuObject;
		bool SetupMainMenuBuilderHook(uintptr_t moduleBase);
	}
}