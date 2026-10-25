#pragma once
#include <cstdint>

namespace HookCrashers::Core::GamePoolExpansion {
bool Install(std::uint32_t budgetMiB);
void Shutdown();
}
