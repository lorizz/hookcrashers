#include "SWFReturnHelper.h"

namespace HookCrashers {
    namespace SWF {
        namespace Helpers {

            Data::SWFReturn* SWFReturnHelper::AsStructured(uint32_t* swfReturnValuePtr) {
                // Direct cast - assumes swfReturnValuePtr points to a memory block
                // suitable for a SWFReturn structure (16 bytes).
                return reinterpret_cast<Data::SWFReturn*>(swfReturnValuePtr);
            }

            // These functions replicate the logic from SWFReturn member functions,
            // but operate directly on the uint32_t array provided by the game.

            void SWFReturnHelper::SetBooleanSuccess(uint32_t* pRet, bool value) {
                if (!pRet) return;
                pRet[0] = 0; // status = success
                pRet[1] = static_cast<uint32_t>(Data::SWFReturn::Type::Boolean); // type
                pRet[2] = 0; // padding
                // Correctly place the bool value in the 32-bit slot
                pRet[3] = 0; // Clear first
                *reinterpret_cast<uint8_t*>(&pRet[3]) = static_cast<uint8_t>(value ? 1 : 0);
            }

            void SWFReturnHelper::SetIntegerSuccess(uint32_t* pRet, int32_t value) {
                if (!pRet) return;
                pRet[0] = 0; // status
                pRet[1] = static_cast<uint32_t>(Data::SWFReturn::Type::Integer); // type
                pRet[2] = 0; // padding
                pRet[3] = static_cast<uint32_t>(value); // value
            }

            void SWFReturnHelper::SetFloatSuccess(uint32_t* pRet, float value) {
                if (!pRet) return;
                pRet[0] = 0; // status
                pRet[1] = static_cast<uint32_t>(Data::SWFReturn::Type::Float); // type
                pRet[2] = 0; // padding
                *reinterpret_cast<float*>(&pRet[3]) = value; // value
            }

            void SWFReturnHelper::SetStringSuccess(uint32_t* pRet, uint16_t stringId) {
                if (!pRet) return;
                pRet[0] = 0; // status
                pRet[1] = static_cast<uint32_t>(Data::SWFReturn::Type::String); // type
                pRet[2] = 0; // padding
                // Correctly place the string ID in the 32-bit slot
                pRet[3] = 0; // Clear first
                *reinterpret_cast<uint16_t*>(&pRet[3]) = stringId;
            }

            void SWFReturnHelper::SetFailure(uint32_t* pRet) {
                if (!pRet) return;
                pRet[0] = 1; // status = failure
                pRet[1] = static_cast<uint32_t>(Data::SWFReturn::Type::None); // type
                pRet[2] = 0; // padding
                pRet[3] = 0; // value
            }

            void SWFReturnHelper::SetVoidSuccess(uint32_t* pRet) {
                if (!pRet) return;
                pRet[0] = 0; // status = success
                pRet[1] = static_cast<uint32_t>(Data::SWFReturn::Type::None); // No type for void
                pRet[2] = 0; // padding
                pRet[3] = 0; // No value
            }


        } // namespace Helpers
    } // namespace SWF
} // namespace HookCrashers