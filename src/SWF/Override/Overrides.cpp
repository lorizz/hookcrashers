#include "Overrides.h"
#include "../../Util/Logger.h"
#include "../Helpers/SWFArgumentReader.h"
#include "../Helpers/SWFReturnHelper.h"
#include "../../Native/NativeFunctions.h"

namespace HookCrashers {
	namespace SWF {
		namespace Override {
			static std::unordered_map<uint16_t, HookFunction> g_overrideFunctions;
			static Util::Logger& L = Util::Logger::Instance();

			bool Register(Data::SWFFunctionID functionId, HookFunction hookFunction) {
				return Register(Data::ToValue(functionId), hookFunction);
			}

			bool Register(uint16_t functionId, HookFunction hookFunction) {
				if (!hookFunction) {
					L.Get()->warn("Attempted to register a null override function for ID: {:#x}", functionId);
					return false;
				}
				g_overrideFunctions[functionId] = hookFunction;
				L.Get()->info("Registered SWF override for function ID: {:#x}", functionId);
				return true;
			}

			HookFunction Get(uint16_t functionId) {
				auto it = g_overrideFunctions.find(functionId);
				if (it != g_overrideFunctions.end()) {
					return it->second;
				}
				return nullptr;
			}

			void InitializeSystem(uintptr_t moduleBase) {
				L.Get()->info("Initializing SWF function override system...");

				// Example: Override ReadStorage (0x3F)
				/*Register(Data::SWFFunctionID::ReadStorage, [](void* thisPtr, int swfContext, uint32_t functionIdRaw, int paramCount,
					Data::SWFArgument** swfArgs, Data::SWFReturn* swfReturn, uint32_t callbackPtr) {
						L.Get()->info("Override executing for ReadStorage (ID: {:#x})", Data::ToValue(Data::SWFFunctionID::ReadStorage));

						float storageIndex = -1;
						int offset = -1;

						if (paramCount >= 1) {
							storageIndex = Helpers::SWFArgumentReader::GetFloat(swfArgs[0], -1);
							L.Get()->debug("Param 0 (Storage Index): {}", storageIndex);
						}
						if (paramCount >= 2) {
							offset = Helpers::SWFArgumentReader::GetInteger(swfArgs[1], -1);
							L.Get()->debug("Param 1 (Offset): {}", offset);
						}

						if (storageIndex < 0) {
							L.Get()->error("ReadStorage override called with invalid storage index.");
							swfReturn->SetFailure();
							return;
						}

						if (offset == 544) {
							L.Get()->info("ReadStorage override: Detected specific offset (544), Returning 0x80.");
							swfReturn->SetIntegerSuccess(0x80);
							return;
						}
					});*/
				L.Get()->info("SWF function override system initialized.");
				L.Get()->flush();
			}
		}
	}
}