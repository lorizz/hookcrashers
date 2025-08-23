#pragma once

#include "../stdafx.h"

namespace HookCrashers {
	namespace Core {
		bool SetupIsFeatureEnabledHook(uintptr_t moduleBase);
		void* GetCorrectObjectManagerPtr();
		uint32_t __fastcall DetouredIsFeatureEnabled(void* thisPtr, void* edxUnused, uint16_t featureId);
		uint32_t GetIsFeatureEnabled(uint16_t featureId);
	}
}