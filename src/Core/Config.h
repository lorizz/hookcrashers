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
};

RuntimeConfig LoadOrCreate(const std::string& moduleDirectory);
const char* ChannelName(UpdateChannel channel);

} // namespace HookCrashers::Core::Config

