#pragma once

#include "../stdafx.h"
#include <cstdint>

namespace HookCrashers {
	namespace Core {
		using AllocateData_t = void* (__thiscall*)(void* this_ptr, uint32_t size);
		extern void* g_pMemoryManager;
		extern AllocateData_t AllocateData;
		bool SetupMainMenuSystemHook(uintptr_t moduleBase);
	}
}