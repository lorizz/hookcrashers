#pragma once

#ifndef HOOKCRASHERS_VERSION_STRING
#define HOOKCRASHERS_VERSION_STRING "5.1.2-dev"
#endif

#ifndef HOOKCRASHERS_CHANNEL_STRING
#define HOOKCRASHERS_CHANNEL_STRING "dev"
#endif

namespace HookCrashers::Core::Version {

constexpr const char* kVersion = HOOKCRASHERS_VERSION_STRING;
constexpr const char* kBuildChannel = HOOKCRASHERS_CHANNEL_STRING;
// Channel manifests are assets of the releases in this repository: the stable
// manifest is attached to the latest (non-prerelease) release, the testing
// manifest to the "dev" prerelease. GitHub redirects both URLs to the asset.
constexpr const char* kStableManifestUrl =
    "https://github.com/lorizz/hookcrashers/releases/latest/download/stable.json";
constexpr const char* kTestingManifestUrl =
    "https://github.com/lorizz/hookcrashers/releases/download/dev/testing.json";

} // namespace HookCrashers::Core::Version
