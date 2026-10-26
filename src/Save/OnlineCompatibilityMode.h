#pragma once

#include <cstdint>

namespace HookCrashers::Save {

enum class OnlineCompatibilityMode : uint8_t {
    Unknown = 0,
    Vanilla = 1,
    Modded = 2
};

void SetOnlineCompatibilityMode(OnlineCompatibilityMode mode);
OnlineCompatibilityMode GetOnlineCompatibilityMode();
const char* OnlineCompatibilityModeName(OnlineCompatibilityMode mode);
bool IsOnlineCompatibilityVanilla();
bool IsOnlineCompatibilityModded();

} // namespace HookCrashers::Save
