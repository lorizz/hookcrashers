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
		struct CharacterData {
			void* vtable;               // Offset 0x00
			char unknown_padding_0[0x10]; // Padding
			int id_1;                   // Offset 0x14
			int id_2;                   // Offset 0x18
			int id_3;                   // Offset 0x1C
			uint32_t flag_1;            // Offset 0x20
			uint32_t flag_2;            // Offset 0x24
			int is_workshop_flag;       // Offset 0x28 - Il flag che hai trovato potrebbe essere qui
			int is_dlc_flag;            // Offset 0x2C - O qui
			// Aggiungi altri campi man mano che li scopri...
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