#include "OnlineCompatibilityMode.h"

#include "../Util/Logger.h"

#include <atomic>

namespace HookCrashers::Save {
namespace {
    std::atomic<int> g_onlineCompatibilityMode{ static_cast<int>(OnlineCompatibilityMode::Unknown) };
}

void SetOnlineCompatibilityMode(OnlineCompatibilityMode mode) {
    const int next = static_cast<int>(mode);
    g_onlineCompatibilityMode.store(next);
}

OnlineCompatibilityMode GetOnlineCompatibilityMode() {
    return static_cast<OnlineCompatibilityMode>(g_onlineCompatibilityMode.load());
}

const char* OnlineCompatibilityModeName(OnlineCompatibilityMode mode) {
    switch (mode) {
        case OnlineCompatibilityMode::Vanilla:
            return "vanilla";
        case OnlineCompatibilityMode::Modded:
            return "modded";
        case OnlineCompatibilityMode::Unknown:
        default:
            return "unknown";
    }
}

bool IsOnlineCompatibilityVanilla() {
    return GetOnlineCompatibilityMode() == OnlineCompatibilityMode::Vanilla;
}

bool IsOnlineCompatibilityModded() {
    return GetOnlineCompatibilityMode() == OnlineCompatibilityMode::Modded;
}

} // namespace HookCrashers::Save


