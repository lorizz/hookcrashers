#pragma once
#include <string>
#include <cstdint>

// Structure representing an argument passed from SWF to native code
struct SWFArgument {
    // There seems to be some data at offset 0x0
    int unknown0;

    // Type identifier at offset 0x4
    enum class Type : int {
        Unknown = 0,
        // Likely other values between 0-1
        Boolean = 2,    // Byte type (0 or 1)
        Integer = 4,    // 32-bit integer
        String = 8,     // String reference/ID
        Float = 0x10    // 32-bit float
    } type;

    // Padding or other fields (offset 0x8)
    int unknown8;

    // The actual value at offset 0xC
    union {
        uint8_t boolValue;    // When type == Boolean
        int32_t intValue;     // When type == Integer
        float floatValue;     // When type == Float
        uint16_t stringId;    // When type == String, this is likely an ID to lookup
    } value;

    // Additional data may follow
};

// Helper class to safely interact with SWF arguments
class SWFArgumentReader {
public:
    // Extract integer value from argument
    static int GetInteger(SWFArgument* arg);

    // Extract boolean value from argument
    static bool GetBoolean(SWFArgument* arg);

    // Extract float value from argument
    static float GetFloat(SWFArgument* arg);

    // Extract string ID from argument
    static uint16_t GetStringId(SWFArgument* arg);

    // Convert argument value to string representation (for logging)
    static std::string GetValueAsString(SWFArgument* arg);

    // Helper to wrap the native GetFirstArgument function
    static int GetFirstArgumentValue(int* swfArgs);

    // Helper to wrap the native GetNextArgument function
    static int GetNextArgumentValue(int* swfArgs);
};