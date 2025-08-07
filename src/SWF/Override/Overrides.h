#pragma once

#include "../../stdafx.h"
#include "../../../include/HookCrashers/Public/Types.h"
#include <functional>

namespace HookCrashers {
	namespace SWF {
		namespace Override {
			using HookFunction = std::function<void(void* thisPtr, int swfContext, uint32_t functionIdRaw, int paramCount,
				Data::SWFArgument** swfArgs, Data::SWFReturn* swfReturn, uint32_t callbackPtr)>;

			bool Register(Data::SWFFunctionID functionId, HookFunction hookFunction);
			bool Register(uint16_t functionId, HookFunction hookFunction);

			HookFunction Get(uint16_t functionId);
			void InitializeSystem(uintptr_t moduleBase);
		}
	}
}