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
			std::string path;
			std::string iconPath;
			bool isScriptMod = false;
			bool hasMainLua = false;
			bool hasLocalizations = false;
			bool hasManifest = false;
			bool hasIcon = false;
			HMODULE handle;
		};

		class ModLoader {
		public:
			static void LoadAllMods();
			static void LoadLegacyBinaryMods();
			static void RegisterScriptMod(
				const std::string& name,
				const std::string& author,
				const std::string& version,
				const std::string& path,
				bool hasMainLua,
				bool hasLocalizations,
				bool hasManifest,
				bool hasIcon,
				const std::string& iconPath);
			static size_t GetModCount();
			static const ModInfo* GetModInfoByIndex(size_t index);

		private:
			static std::vector<ModInfo> s_loadedMods;
		};
	}
}
