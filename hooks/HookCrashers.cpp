#include "HookCrashers.h"
#include "../logger.h"
#include <unordered_map>
#include "CustomSWFFunctions.h"

namespace HookCrashers {
	Logger& l = Logger::Instance();
	static std::unordered_map<uint16_t, HookFunction> hookFunctions;
	static uintptr_t g_moduleBase = 0;

	bool Override(SWFFunctionID functionId, HookFunction hookFunction) {
		return Override(ToValue(functionId), hookFunction);
	}

	bool Override(uint16_t functionId, HookFunction hookFunction) {
		hookFunctions[functionId] = hookFunction;
		l.Get()->info("Registered hook for function ID: {:#x}", functionId);
		return true;
	}

	HookFunction GetHookFunction(uint16_t functionId) {
		auto it = hookFunctions.find(functionId);
		if (it != hookFunctions.end()) {
			return it->second;
		}
		return nullptr;
	}

	static SWFArgument** ConvertRawArgs(int* rawSwfArgs, int paramCount) {
		if (paramCount <= 0 || !rawSwfArgs) return nullptr;

		SWFArgument** args = new SWFArgument*[paramCount];

		for (int i = 0; i < paramCount; i++) {
			args[i] = reinterpret_cast<SWFArgument*>(rawSwfArgs[i]);
		}

		return args;
	}

	static void CleanupArgs(SWFArgument** args) {
		if (args) {
			delete[] args;
		}
	}

	void Initialize(uintptr_t moduleBase) {
		g_moduleBase = moduleBase;
		CustomSWFFunctions::Initialize();
		l.Get()->info("HookCrashers system initialized");
	}
}