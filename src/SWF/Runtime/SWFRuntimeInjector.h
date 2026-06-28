#pragma once

#include "../SWFStructures.h"
#include <cstdint>
#include <string>

namespace HookCrashers::SWF::Runtime {
	bool TryInjectLobbyPortraits(SWFScene* scene, const std::string& swfPath);
	bool IsInjectedPortraitShape(uint16_t shapeId, uint16_t* depthOut = nullptr);
	bool TryPatchDisplayObjectSortDepth(void* displayObject, int depthValue);
}
