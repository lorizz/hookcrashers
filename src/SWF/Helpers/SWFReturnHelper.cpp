#include "SWFReturnHelper.h"
#include "../../stdafx.h" // Ok to include stdafx in a .cpp file

// For convenience, use the same alias as the header
using HC_SWFReturn = HookCrashers::SWF::Data::SWFReturn;

namespace HookCrashers {
	namespace SWF {
		namespace Helpers {
			// ADD THIS IMPLEMENTATION
			HC_SWFReturn* SWFReturnHelper::AsStructured(uint32_t* swfReturnRaw) {
				// This is the core of the function: treat the block of memory
				// pointed to by the raw pointer as a pointer to our struct.
				return reinterpret_cast<HC_SWFReturn*>(swfReturnRaw);
			}

			// FIXED: Use the alias for consistency in all function definitions
			void SWFReturnHelper::SetBooleanSuccess(HC_SWFReturn* swfReturn, bool val) {
				if (!swfReturn) return;
				swfReturn->status = 0;
				swfReturn->type = HC_SWFReturn::Type::Boolean;
				swfReturn->padding = 0;
				swfReturn->value.rawValue = 0;
				swfReturn->value.boolValue = static_cast<uint8_t>(val ? 1 : 0);
			}

			void SWFReturnHelper::SetIntegerSuccess(HC_SWFReturn* swfReturn, int32_t val) {
				if (!swfReturn) return;
				swfReturn->status = 0;
				swfReturn->type = HC_SWFReturn::Type::Integer;
				swfReturn->padding = 0;
				swfReturn->value.intValue = val;
			}

			void SWFReturnHelper::SetFloatSuccess(HC_SWFReturn* swfReturn, float val) {
				if (!swfReturn) return;
				swfReturn->status = 0;
				swfReturn->type = HC_SWFReturn::Type::Float;
				swfReturn->padding = 0;
				swfReturn->value.floatValue = val;
			}

			void SWFReturnHelper::SetStringSuccess(HC_SWFReturn* swfReturn, uint16_t stringId) {
				if (!swfReturn) return;
				swfReturn->status = 0;
				swfReturn->type = HC_SWFReturn::Type::String;
				swfReturn->padding = 0;
				swfReturn->value.rawValue = 0;
				swfReturn->value.stringId = stringId;
			}

			void SWFReturnHelper::SetFailure(HC_SWFReturn* swfReturn) {
				if (!swfReturn) return;
				swfReturn->status = 1;
				swfReturn->type = HC_SWFReturn::Type::None;
				swfReturn->padding = 0;
				swfReturn->value.rawValue = 0;
			}
		}
	}
}