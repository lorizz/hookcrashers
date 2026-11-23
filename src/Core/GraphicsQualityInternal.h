#pragma once
#include <algorithm>
#include <cstdint>

namespace HookCrashers::Core::GraphicsQuality::Detail {
enum class Quality : int { Low=0, Medium=1, High=2, Ultra=3 };

inline int DecodeSetting(int value, int schema, int maximum) {
    // Existing Castle Crashers/HC <=5.1.1 files use 2 for Ultra.
    if (schema < 1 && value == 2) value=3;
    return (std::clamp)(value,0,maximum);
}
inline std::uint32_t HistoricalHighDimension(std::uint32_t value, bool powerOfTwo,
                                           std::uint32_t minimum) {
    // Port of pre-Painter VA 459CA0: round first, then independently clamp
    // each axis. Bounding before rounding also avoids uint32 wraparound.
    if (powerOfTwo && value < 512) {
        std::uint32_t rounded=1;
        while (rounded<value) rounded*=2;
        value=rounded;
    }
    if (value<minimum) return minimum;
    return (std::min)(value,std::uint32_t{512});
}
}
