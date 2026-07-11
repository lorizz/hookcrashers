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
};

RuntimeConfig LoadOrCreate(const std::string& moduleDirectory);
const char* ChannelName(UpdateChannel channel);

} // namespace HookCrashers::Core::Config

