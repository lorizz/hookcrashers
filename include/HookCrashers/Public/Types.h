#pragma once

#include <cstdint> // Use standard types, not stdafx.h

// This header defines all the public data structures needed to interact
// with the HookCrashers API. It should be included by both the API
// and any external projects that use the API.

namespace HookCrashers {
	namespace Game {
		struct PlayerInputSource;
		struct MenuManager {
			char unknown_padding_0[0xE0];
			int32_t currentButtonIndex;
			uint32_t lastPolledInput;
			char unknown_padding_1[0x4];
			uint16_t menuStateFlags;
		};
	}
	namespace SWF {
		namespace Data {

			// Forward declare to be used in the callback typedef
			struct SWFArgument;
			struct SWFReturn;

			// --- Public Structs ---

			struct SWFArgument {
				int unknown0;

				enum class Type : int {
					Unknown = 0,
					Boolean = 2,
					Integer = 4,
					String = 8,
					Float = 0x10,
				} type;

				int unknown8;

				union {
					uint8_t boolValue;
					int32_t intValue;
					float floatValue;
					uint16_t stringId;
					uint32_t rawValue;
				} value;
			};
			static_assert(sizeof(SWFArgument) == 0x10, "SWFArgument size mismatch");

			struct SWFReturn {
				uint32_t status; // Offset 0x0: 0 = success, 1 = failure/error

				enum class Type : uint32_t {
					None = 0,
					Boolean = 2,
					Integer = 4,
					String = 8,
					Float = 0x10,
				} type;

				uint32_t padding;

				union {
					uint8_t boolValue;
					int32_t intValue;
					float floatValue;
					uint16_t stringId;
					uint32_t rawValue;
				} value;
			};
			static_assert(sizeof(SWFReturn) == 0x10, "SWFReturn size mismatch");

			// --- Public Enums ---

			enum class SWFFunctionID : uint16_t {
				ReadStorage = 0x3F,
				WriteStorage = 0x40,
				SetFlashController = 0x91,

				// Custom function IDs (start from a high number)
				CustomBase = 50000,
				HelloWorld = 50001,
				GetHookCrashersVersion = 50002,
				GetModCount = 50003,
				GetModInfo = 50004,
			};

		} // namespace Data
	} // namespace SWF
} // namespace HookCrashers