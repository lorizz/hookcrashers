#pragma once

#include "../stdafx.h"

namespace HookCrashers {
	bool SetupAttachSkinGraphicHook(uintptr_t moduleBase);
	void ResetGraphicShiftState();
}