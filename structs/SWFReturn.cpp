#include "SWFReturn.h"

// SWFReturn implementation

void SWFReturn::SetBooleanSuccess(uint8_t val) {
    status = 0;
    type = Type::Boolean;
    padding = 0;
    value.boolValue = val;
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

void SWFReturn::SetStringSuccess(uint16_t val) {
    status = 0;
    type = Type::String;
    padding = 0;
    value.stringId = val;
}

void SWFReturn::SetFailure() {
    status = 1;
    type = Type::None;
    padding = 0;
    value.rawValue = 0;
}

// SWFReturnHelper implementation

SWFReturn* SWFReturnHelper::AsStructured(uint32_t* swfReturnValue) {
    return reinterpret_cast<SWFReturn*>(swfReturnValue);
}

void SWFReturnHelper::SetBooleanSuccess(uint32_t* swfReturnValue, uint8_t value) {
    swfReturnValue[0] = 0;                  // Success status
    swfReturnValue[1] = static_cast<uint32_t>(SWFReturn::Type::Boolean); // Type = Boolean/byte
    swfReturnValue[2] = 0;                  // Padding
    *reinterpret_cast<uint8_t*>(&swfReturnValue[3]) = value; // The actual value
}

void SWFReturnHelper::SetIntegerSuccess(uint32_t* swfReturnValue, int32_t value) {
    swfReturnValue[0] = 0;                  // Success status
    swfReturnValue[1] = static_cast<uint32_t>(SWFReturn::Type::Integer); // Type = Integer
    swfReturnValue[2] = 0;                  // Padding
    swfReturnValue[3] = static_cast<uint32_t>(value); // The actual value
}

void SWFReturnHelper::SetFloatSuccess(uint32_t* swfReturnValue, float value) {
    swfReturnValue[0] = 0;                  // Success status
    swfReturnValue[1] = static_cast<uint32_t>(SWFReturn::Type::Float); // Type = Float
    swfReturnValue[2] = 0;                  // Padding
    *reinterpret_cast<float*>(&swfReturnValue[3]) = value; // The actual value
}

void SWFReturnHelper::SetStringSuccess(uint32_t* swfReturnValue, uint16_t value) {
    swfReturnValue[0] = 0;                  // Success status
    swfReturnValue[1] = static_cast<uint32_t>(SWFReturn::Type::String); // Type = String
    swfReturnValue[2] = 0;                  // Padding
    *reinterpret_cast<uint16_t*>(&swfReturnValue[3]) = value; // The actual value
}

void SWFReturnHelper::SetFailure(uint32_t* swfReturnValue) {
    swfReturnValue[0] = 1;                  // Failure status
    swfReturnValue[1] = 0;                  // No type
    swfReturnValue[2] = 0;                  // Padding
    swfReturnValue[3] = 0;                  // No value
}