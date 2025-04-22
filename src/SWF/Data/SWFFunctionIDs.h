#pragma once

#include <cstdint>

namespace HookCrashers {
	namespace SWF {
		namespace Data {
			enum class SWFFunctionID : uint16_t {
				ReadStorage = 0x3F,
				WriteStorage = 0x40,

				// Custom function IDs (start from a high number)
				CustomBase = 50000,
				HelloWorld = 50001,
			};

			inline uint16_t ToValue(SWFFunctionID id) {
				return static_cast<uint16_t>(id);
			}
		}
	}
}