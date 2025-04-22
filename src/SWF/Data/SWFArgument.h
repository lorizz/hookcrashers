#pragma once

#include "../../stdafx.h"

namespace HookCrashers {
	namespace SWF {
		namespace Data {
			struct SWFArgument {
				int unknown0; // Offset 0x0

				enum class Type : int {
					Unknown = 0,
					Boolean = 2,
					Integer = 4,
					String = 8,
					Float = 0x10,
				} type;

				int unknown8; // Offset 0x8 (Padding or metadata?)

				union {
					uint8_t boolValue;
					int32_t intValue;
					float floatValue;
					uint16_t stringId;
					uint32_t rawValue;
				} value;
			};
			static_assert(sizeof(SWFArgument) == 0x10, "SWFArgument size mismatch");
		}
	}
}