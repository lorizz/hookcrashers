#pragma once

#ifndef HOOKCRASHERS_VERSION_STRING
#define HOOKCRASHERS_VERSION_STRING "5.0.66-dev"
#endif

#ifndef HOOKCRASHERS_CHANNEL_STRING
#define HOOKCRASHERS_CHANNEL_STRING "dev"
#endif

namespace HookCrashers::Core::Version {

constexpr const char* kVersion = HOOKCRASHERS_VERSION_STRING;
constexpr const char* kBuildChannel = HOOKCRASHERS_CHANNEL_STRING;
constexpr const char* kStableManifestUrl =
    "https://raw.githubusercontent.com/lorizz/hookcrashers-releases/main/stable.json";
constexpr const char* kTestingManifestUrl =
    "https://raw.githubusercontent.com/lorizz/hookcrashers-releases/main/testing.json";

} // namespace HookCrashers::Core::Version
