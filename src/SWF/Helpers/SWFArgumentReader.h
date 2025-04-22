#pragma once

#include "../../stdafx.h"
#include "../Data/SWFArgument.h"

namespace HookCrashers {
	namespace SWF {
		namespace Helpers {
			class SWFArgumentReader {
			public:
				static int32_t GetInteger(const Data::SWFArgument* arg, int32_t defaultVal = 0);
				static bool GetBoolean(const Data::SWFArgument* arg, bool defaultVal = false);
				static float GetFloat(const Data::SWFArgument* arg, float defaultVal = 0.0f);
				static uint16_t GetStringId(const Data::SWFArgument* arg, uint16_t defaultVal = 0);

				static std::string GetString(const Data::SWFArgument* arg, const std::string& defaultVal = "");

				static std::string GetValueAsString(const Data::SWFArgument* arg);

				template <typename T>
				static T GetValue(const Data::SWFArgument* arg, T defaultVal);
			};

			template <>
			inline int32_t SWFArgumentReader::GetValue<int32_t>(const Data::SWFArgument* arg, int32_t defaultVal) {
				return GetInteger(arg, defaultVal);
			}

			template <>
			inline bool SWFArgumentReader::GetValue<bool>(const Data::SWFArgument* arg, bool defaultVal) {
				return GetBoolean(arg, defaultVal);
			}

			template <>
			inline float SWFArgumentReader::GetValue<float>(const Data::SWFArgument* arg, float defaultVal) {
				return GetFloat(arg, defaultVal);
			}

			template <>
			inline std::string SWFArgumentReader::GetValue<std::string>(const Data::SWFArgument* arg, std::string defaultVal) {
				return GetString(arg, defaultVal);
			}
		}
	}
}