#pragma once

#include <cstdint>
#include <functional>

namespace HookCrashers {
	namespace SWF {
		namespace Data {
			struct SWFArgument;
			struct SWFReturn;
			enum class SWFFunctionID : uint16_t;
			uint16_t ToValue(SWFFunctionID id);
		}
	}
}

#include "../src/SWF/Data/SWFFunctionIDs.h"

namespace HookCrashers {
	bool Initialize(void* hModuleGame);
	void Shutdown();

	namespace SWF {
		namespace Override {
			using HookFunction = std::function<void(void* thisPtr, int swfContext, uint32_t functionIdRaw, int paramCount,
				Data::SWFArgument** swfArgs, Data::SWFReturn* swfReturn, uint32_t callbackPtr)>;

			/**
			 * @brief Overrides a standard game SWF function.
			 * @param functionId The ID of the function to override (e.g., SWFFunctionID::ReadStorage).
			 * @param hookFunction Your function that will be called instead of the original.
			 * @return True on success.
			 */
			bool Register(Data::SWFFunctionID functionId, HookFunction hookFunction);
			bool Register(uint16_t functionId, HookFunction hookFunction);
		}
		namespace Custom {
			using CustomSWFFunction = std::function<void(int paramCount, Data::SWFArgument** swfArgs, Data::SWFReturn* swfReturn)>;

			/**
			 * @brief Registers a new custom function that can be called from SWF.
			 * @param functionId The ID for your custom function (must be >= 10000).
			 * @param function The C++ function to execute when the custom function is called from SWF.
			 * @return True on success.
			 */
			bool Register(Data::SWFFunctionID functionId, CustomSWFFunction function);
			bool Register(uint16_t functionId, CustomSWFFunction function); // Allow raw ID registration
		}
	}
}
