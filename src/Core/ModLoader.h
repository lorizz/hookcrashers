#pragma once

#include "../stdafx.h"
#include <vector>
#include <string>
#include <windows.h>

namespace HookCrashers {
	namespace Core {
		struct ModInfo {
			std::string name;
			std::string author;
			std::string version;
			HMODULE handle;
		};

		class ModLoader {
		public:
			static void LoadAllMods();
			static size_t GetModCount();
			static const ModInfo* GetModInfoByIndex(size_t index);

		private:
			static std::vector<ModInfo> s_loadedMods;
		};
	}
}