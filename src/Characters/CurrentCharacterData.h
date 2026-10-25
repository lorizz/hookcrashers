#pragma once
#include "../../sdk/include/CurrentCharacterData.h"

namespace HookCrashers::Characters::CurrentData {
bool Install();
void Shutdown();
// Called only at a completed VM/frame boundary on its owning thread.
void CaptureFromContext(void* context);
}
