#pragma once

#include "../../stdafx.h"
#include "../../../include/HookCrashers/Public/Types.h" // The one source of truth for types
#include <functional>
#include <vector>
#include <string>

// For convenience, define aliases for the public types that this system will use.
using HC_SWFArgument = HookCrashers::SWF::Data::SWFArgument;
using HC_SWFReturn = HookCrashers::SWF::Data::SWFReturn;
using HC_SWFFunctionID = HookCrashers::SWF::Data::SWFFunctionID;

namespace HookCrashers {
	namespace SWF {
		namespace Custom {

			// FIXED: The function signature for our C++ callback type now uses the public types.
			using CustomSWFFunction = std::function<void(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn)>;

			// Register functions are now declared correctly
			bool Register(HC_SWFFunctionID functionId, const std::string& functionName, CustomSWFFunction function);
			bool Register(uint16_t functionId, const std::string& functionName, CustomSWFFunction function);

			bool IsCustom(uint16_t id);

			// HandleCall's signature must be correct
			bool HandleCall(uint16_t functionId, void* thisPtr, int swfContext,
				int paramCount, HC_SWFArgument** args, HC_SWFReturn* swfReturn, uint32_t callbackPtr);

			std::vector<uint16_t> GetRegisteredIds();
			std::string GetRegisteredName(uint16_t id);
			void InitializeSystem();
			void RegisterAllWithGame(void* gameThisPtr, uintptr_t originalRegisterFuncAddr);
		}
	}
}