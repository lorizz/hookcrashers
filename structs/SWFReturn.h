#pragma once
#include <cstdint>

// Structure representing a return value from native code back to SWF
struct SWFReturn {
    // Status field at offset 0x0
    // 0 = success, 1 = error/failure
    uint32_t status;

    // Type identifier at offset 0x4
    enum class Type : uint32_t {
        None = 0,
        // Likely other values between 0-1
        Boolean = 2,    // Byte type (0 or 1)
        Integer = 4,    // 32-bit integer
        String = 8,     // String reference/ID
        Float = 0x10    // 32-bit float
    } type;

    // Padding at offset 0x8 (observed in original code)
    uint32_t padding;

    // The actual value at offset 0xC
    union {
        uint8_t boolValue;    // When type == Boolean
        int32_t intValue;     // When type == Integer
        float floatValue;     // When type == Float
        uint16_t stringId;    // When type == String
        uint32_t rawValue;    // For direct access
    } value;

    // Helper methods
    void SetBooleanSuccess(uint8_t value);
    void SetIntegerSuccess(int32_t value);
    void SetFloatSuccess(float value);
    void SetStringSuccess(uint16_t value);
    void SetFailure();
};

// Helper class to work with raw swfReturnValue arrays
class SWFReturnHelper {
public:
    // Convert raw swfReturnValue pointer to our structured type
    static SWFReturn* AsStructured(uint32_t* swfReturnValue);

    // Set a boolean return value
    static void SetBooleanSuccess(uint32_t* swfReturnValue, uint8_t value);

    // Set an integer return value
    static void SetIntegerSuccess(uint32_t* swfReturnValue, int32_t value);

    // Set a float return value
    static void SetFloatSuccess(uint32_t* swfReturnValue, float value);

    // Set a string ID return value
    static void SetStringSuccess(uint32_t* swfReturnValue, uint16_t value);

    // Set a failure return value
    static void SetFailure(uint32_t* swfReturnValue);
};