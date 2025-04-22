#include "SWFReturn.h"

namespace HookCrashers {
	namespace SWF {
		namespace Data {
			void SWFReturn::SetBooleanSuccess(bool val) {
				status = 0;
				type = Type::Boolean;
				padding = 0;
				value.rawValue = 0;
				value.boolValue = static_cast<uint8_t>(val ? 1 : 0);
			}

			void SWFReturn::SetIntegerSuccess(int32_t val) {
				status = 0;
				type = Type::Integer;
				padding = 0;
				value.intValue = val;
			}

			void SWFReturn::SetFloatSuccess(float val) {
				status = 0;
				type = Type::Float;
				padding = 0;
				value.floatValue = val;
			}

			void SWFReturn::SetStringSuccess(uint16_t stringId) {
				status = 0;
				type = Type::String;
				padding = 0;
				value.rawValue = 0;
				value.stringId = stringId;
			}

			void SWFReturn::SetFailure() {
				status = 1;
				type = Type::None;
				padding = 0;
				value.rawValue = 0;
			}
		}
	}
}