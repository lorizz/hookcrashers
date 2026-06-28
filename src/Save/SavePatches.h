#pragma once

#include "../stdafx.h"
#include <string>

namespace HookCrashers::Save {
	void RegisterSaveName(const std::string& name);
	const std::string& GetSaveFileName();
	const std::string& GetSaveBackupFileName();
	bool ApplySaveFileNamePatch();
	bool ApplySaveExpansionPatches();
}