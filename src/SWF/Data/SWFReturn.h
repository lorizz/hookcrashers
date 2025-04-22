#pragma once

#include "../../stdafx.h"

namespace HookCrashers {
	namespace SWF {
		namespace Data {
			struct SWFReturn {
				uint32_t status; // Offset 0x0: 0 = success, 1 = failure/error

				enum class Type : uint32_t {
					None = 0,
					Boolean = 2,
					Integer = 4,
					String = 8,
					Float = 0x10,
				} type;

				uint32_t padding; // Offset 0x8: Seems consistently zeroed

				union {
					uint8_t boolValue;
					int32_t intValue;
					float floatValue;
					uint16_t stringId;
					uint32_t rawValue;
				} value;

				void SetBooleanSuccess(bool val);
				void SetIntegerSuccess(int32_t val);
				void SetFloatSuccess(float val);
				void SetStringSuccess(uint16_t stringId);
				void SetFailure();
			};
			static_assert(sizeof(SWFReturn) == 0x10, "SWFReturn size mismatch");
		}
	}
}