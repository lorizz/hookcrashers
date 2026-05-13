#pragma once

#include "../SWFStructures.h"
#include <string>

namespace HookCrashers::SWF::Runtime {
	bool TryInjectLobbyPortraits(SWFScene* scene, const std::string& swfPath);
}
