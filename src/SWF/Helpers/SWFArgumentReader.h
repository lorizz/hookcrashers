#pragma once

#include "../../stdafx.h"
#include "../../../include/HookCrashers/Public/Types.h" // This include is correct!

// For convenience
using HC_SWFArgument = HookCrashers::SWF::Data::SWFArgument;

namespace HookCrashers {
	namespace SWF {
		namespace Helpers {
			class SWFArgumentReader {
			public:
				// FIXED: Use the alias for the public type
				static int32_t GetInteger(const HC_SWFArgument* arg, int32_t defaultVal = 0);
				static bool GetBoolean(const HC_SWFArgument* arg, bool defaultVal = false);
				static float GetFloat(const HC_SWFArgument* arg, float defaultVal = 0.0f);
				static uint16_t GetStringId(const HC_SWFArgument* arg, uint16_t defaultVal = 0);
				static std::string GetString(const HC_SWFArgument* arg, const std::string& defaultVal = "");
				static std::string GetValueAsString(const HC_SWFArgument* arg);

				template <typename T>
				static T GetValue(const HC_SWFArgument* arg, T defaultVal);
			};

			// Template specializations also need to be updated
			template <>
			inline int32_t SWFArgumentReader::GetValue<int32_t>(const HC_SWFArgument* arg, int32_t defaultVal) {
				return GetInteger(arg, defaultVal);
			}

			template <>
			inline bool SWFArgumentReader::GetValue<bool>(const HC_SWFArgument* arg, bool defaultVal) {
				return GetBoolean(arg, defaultVal);
			}

			template <>
			inline float SWFArgumentReader::GetValue<float>(const HC_SWFArgument* arg, float defaultVal) {
				return GetFloat(arg, defaultVal);
			}

			template <>
			inline std::string SWFArgumentReader::GetValue<std::string>(const HC_SWFArgument* arg, std::string defaultVal) {
				return GetString(arg, defaultVal);
			}
		}
	}
}