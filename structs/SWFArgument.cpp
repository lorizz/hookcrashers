#include "SWFArgument.h"

int SWFArgumentReader::GetInteger(SWFArgument* arg) {
    if (!arg || arg->type != SWFArgument::Type::Integer)
        return 0;
    return arg->value.intValue;
}

bool SWFArgumentReader::GetBoolean(SWFArgument* arg) {
    if (!arg || arg->type != SWFArgument::Type::Boolean)
        return false;
    return arg->value.boolValue != 0;
}

float SWFArgumentReader::GetFloat(SWFArgument* arg) {
    if (!arg || arg->type != SWFArgument::Type::Float)
        return 0.0f;
    return arg->value.floatValue;
}

uint16_t SWFArgumentReader::GetStringId(SWFArgument* arg) {
    if (!arg || arg->type != SWFArgument::Type::String)
        return 0;
    return arg->value.stringId;
}

std::string SWFArgumentReader::GetValueAsString(SWFArgument* arg) {
    if (!arg) return "null";

    switch (static_cast<int>(arg->type)) {
    case static_cast<int>(SWFArgument::Type::Boolean):
        return std::to_string(static_cast<int>(arg->value.boolValue));
    case static_cast<int>(SWFArgument::Type::Integer):
        return std::to_string(arg->value.intValue);
    case static_cast<int>(SWFArgument::Type::Float):
        return std::to_string(arg->value.floatValue);
    case static_cast<int>(SWFArgument::Type::String):
        return "StringID: " + std::to_string(arg->value.stringId);
    default:
        return "Unknown type: " + std::to_string(static_cast<int>(arg->type));
    }
}

// These implementations assume you have declared the native function types somewhere
// You'll need to adjust these according to your actual native function declarations

int SWFArgumentReader::GetFirstArgumentValue(int* swfArgs) {
    // This is a wrapper for your NativeHooks::CallNative<uint32_t>(Natives::GetFirstArgument, swfArgs)
    // Replace with your actual implementation
    SWFArgument* arg = reinterpret_cast<SWFArgument*>(*swfArgs);
    return GetInteger(arg);
}

int SWFArgumentReader::GetNextArgumentValue(int* swfArgs) {
    // This is a wrapper for your NativeHooks::CallNative<uint32_t>(Natives::GetNextArgument, swfArgs)
    // Replace with your actual implementation
    // Note: This is just a placeholder - you'll need to implement based on how GetNextArgument works
    return 0;
}