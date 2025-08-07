#pragma once

#include "../stdafx.h"
#include <cstdint>

namespace HookCrashers {
	namespace Core {
		extern void* g_pModLoaderMenuObject;
		bool SetupSceneConstructorHook(uintptr_t moduleBase);
	}
}