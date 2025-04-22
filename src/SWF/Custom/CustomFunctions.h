#pragma once

#include "../../stdafx.h"
#include "../Data/SWFArgument.h"
#include "../Data/SWFReturn.h"
#include "../Data/SWFFunctionIDs.h"
#include <functional>
#include <vector>
#include <string>

namespace HookCrashers {
	namespace SWF {
		namespace Custom {
			using CustomSWFFunction = std::function<void(int paramCount, Data::SWFArgument** swfARgs, Data::SWFReturn* swfReturn)>;

			void HelloWorldHandler(int paramCount, Data::SWFArgument** swfArgs, Data::SWFReturn* swfReturn);

			bool Register(Data::SWFFunctionID functionId, const std::string& functionName, CustomSWFFunction function);
			bool Register(uint16_t functionId, const std::string& functionName, CustomSWFFunction function);

			bool IsCustom(uint16_t id);

			bool HandleCall(uint16_t functionId, void* thisPtr, int swfContext,
				int paramCount, Data::SWFArgument** args, Data::SWFReturn* swfReturn, uint32_t callbackPtr);

			std::vector<uint16_t> GetRegisteredIds();

			std::string GetRegisteredName(uint16_t id);

			void InitializeSystem();

			void RegisterAllWithGame(void* gameThisPtr, uintptr_t originalRegisterFuncAddr);
		}
	}
}