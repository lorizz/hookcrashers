#include "Overrides.h"
#include "../Dispatcher/Dispatcher.h"
#include "../../Util/Logger.h"
#include "../Helpers/SWFArgumentReader.h"
#include "../Helpers/SWFReturnHelper.h"
#include "../../Native/NativeFunctions.h"

// Define our aliases for the public types
using HC_SWFArgument = HookCrashers::SWF::Data::SWFArgument;
using HC_SWFReturn = HookCrashers::SWF::Data::SWFReturn;
using HC_SWFFunctionID = HookCrashers::SWF::Data::SWFFunctionID;

namespace HookCrashers {
	namespace SWF {
		namespace Override {
			static std::unordered_map<uint16_t, HookFunction> g_overrideFunctions;
			static Util::Logger& L = Util::Logger::Instance();

			// FIXED: Use the alias for the enum type
			bool Register(Data::SWFFunctionID functionId, HookFunction hookFunction) {
				// FIXED: Use a static_cast to convert from enum to underlying type
				return Register(static_cast<uint16_t>(functionId), hookFunction);
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

			void Override_SetFlashController(void* thisPtr, int swfContext, uint32_t functionIdRaw, int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn, uint32_t callbackPtr) {

				if (paramCount > 0) {
					const HC_SWFArgument* firstArg = swfArgs[0];
					std::string controllerName = Helpers::SWFArgumentReader::GetString(firstArg, "N/A");
					if (controllerName == "asdino") {
						L.Get()->info("SetFlashController -> Controller Name: 'asdino'");
						L.Get()->flush();
					}
				}
				//L.Get()->info("SetFlashController -> Calling original function");
				Dispatcher::CallOriginal(thisPtr, swfContext, functionIdRaw, paramCount, swfArgs, reinterpret_cast<uint32_t*>(swfReturn), callbackPtr);
			}

			void InitializeSystem(uintptr_t moduleBase) {
				L.Get()->info("Initializing SWF function override system...");

				Register(HC_SWFFunctionID::ReadStorage, [](void* thisPtr, int swfContext, uint32_t functionIdRaw, int paramCount,
					HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn, uint32_t callbackPtr) {
					Dispatcher::CallOriginal(thisPtr, swfContext, functionIdRaw, paramCount, swfArgs, reinterpret_cast<uint32_t*>(swfReturn), callbackPtr);
				});

				// Example: Override ReadStorage (0x3F)
				/*Register(HC_SWFFunctionID::ReadStorage, [](void* thisPtr, int swfContext, uint32_t functionIdRaw, int paramCount,
					// FIXED: Use the correct public types in the lambda signature
					HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn, uint32_t callbackPtr) {
						L.Get()->info("Override executing for ReadStorage (ID: {:#x})", static_cast<uint16_t>(HC_SWFFunctionID::ReadStorage));

						// ... logic is fine ...

						if (storageIndex < 0) {
							L.Get()->error("ReadStorage override called with invalid storage index.");
							// FIXED: Call the static helper function
							Helpers::SWFReturnHelper::SetFailure(swfReturn);
							return;
						}

						if (offset == 544) {
							L.Get()->info("ReadStorage override: Detected specific offset (544), Returning 0x80.");
							// FIXED: Call the static helper function
							Helpers::SWFReturnHelper::SetIntegerSuccess(swfReturn, 0x80);
							return;
						}
					});*/
				//L.Get()->info("Overriding SetFlashController");
				//Register(HC_SWFFunctionID::SetFlashController, Override_SetFlashController);
				L.Get()->info("SWF function override system initialized.");
				L.Get()->flush();
			}
		}
	}
}