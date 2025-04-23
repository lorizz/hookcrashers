#include "SWFReturnHelper.h"

namespace HookCrashers {
    namespace SWF {
        namespace Helpers {

            Data::SWFReturn* SWFReturnHelper::AsStructured(uint32_t* swfReturnValuePtr) {
                return reinterpret_cast<Data::SWFReturn*>(swfReturnValuePtr);
            }

            void SWFReturnHelper::SetBooleanSuccess(uint32_t* pRet, bool value) {
                if (!pRet) return;
                pRet[0] = 0;
                pRet[1] = static_cast<uint32_t>(Data::SWFReturn::Type::Boolean);
                pRet[2] = 0;
                pRet[3] = 0;
                *reinterpret_cast<uint8_t*>(&pRet[3]) = static_cast<uint8_t>(value ? 1 : 0);
            }

            void SWFReturnHelper::SetIntegerSuccess(uint32_t* pRet, int32_t value) {
                if (!pRet) return;
                pRet[0] = 0;
                pRet[1] = static_cast<uint32_t>(Data::SWFReturn::Type::Integer);
                pRet[2] = 0;
                pRet[3] = static_cast<uint32_t>(value);
            }

            void SWFReturnHelper::SetFloatSuccess(uint32_t* pRet, float value) {
                if (!pRet) return;
                pRet[0] = 0;
                pRet[1] = static_cast<uint32_t>(Data::SWFReturn::Type::Float);
                pRet[2] = 0;
                *reinterpret_cast<float*>(&pRet[3]) = value;
            }

            void SWFReturnHelper::SetStringSuccess(uint32_t* pRet, uint16_t stringId) {
                if (!pRet) return;
                pRet[0] = 0;
                pRet[1] = static_cast<uint32_t>(Data::SWFReturn::Type::String);
                pRet[2] = 0;
                pRet[3] = 0;
                *reinterpret_cast<uint16_t*>(&pRet[3]) = stringId;
            }

            void SWFReturnHelper::SetFailure(uint32_t* pRet) {
                if (!pRet) return;
                pRet[0] = 1;
                pRet[1] = static_cast<uint32_t>(Data::SWFReturn::Type::None);
                pRet[2] = 0;
                pRet[3] = 0;
            }

            void SWFReturnHelper::SetVoidSuccess(uint32_t* pRet) {
                if (!pRet) return;
                pRet[0] = 0;
                pRet[1] = static_cast<uint32_t>(Data::SWFReturn::Type::None);
                pRet[2] = 0;
                pRet[3] = 0;
            }


        }
    }
}