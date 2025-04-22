#pragma once

#include "../stdafx.h"

namespace HookCrashers {
	namespace Core {
		bool SetupRegisterSWFFunctionHook(uintptr_t moduleBase);
		void* GetCorrectThisPtr();
		void __fastcall DetouredRegisterSWFFunction(void* thisPtr, void* edxUnused, uint16_t functionId, const char* functionName);
	}
}