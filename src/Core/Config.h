#pragma once

#include <string>

namespace HookCrashers::Core::Config {

enum class UpdateChannel {
    Stable,
    Testing
};

struct RuntimeConfig {
    bool updatesEnabled = true;
    UpdateChannel channel = UpdateChannel::Stable;
    std::string testingKey;
    bool showExternalConsole = false;
    bool enableOverlay = true;
    unsigned int overlayToggleKey = 0x24; // VK_HOME
    unsigned int gamePoolMiB = 1024; // 576 MiB original pool plus on-demand overflow arenas.
};

RuntimeConfig LoadOrCreate(const std::string& moduleDirectory);
const char* ChannelName(UpdateChannel channel);

} // namespace HookCrashers::Core::Config

