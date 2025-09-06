#include "CustomFunctions.h"
#include "../../Util/Logger.h"
#include "../Helpers/SWFArgumentReader.h"
#include "../Helpers/SWFReturnHelper.h"
#include "../../Core/RegisterSWFFunctionHook.h"
#include <unordered_map>
#include <stdexcept>

// The aliases are defined in the header and are now available here.

namespace HookCrashers {
	namespace SWF {
		namespace Custom {
			static Util::Logger& L = Util::Logger::Instance();
			static std::unordered_map<uint16_t, CustomSWFFunction> g_customFunctions;
			static std::unordered_map<uint16_t, std::string> g_customFunctionNames;

			bool Register(HC_SWFFunctionID functionId, const std::string& functionName, CustomSWFFunction function) {
				// FIXED: Use a static_cast to convert from enum to underlying type
				return Register(static_cast<uint16_t>(functionId), functionName, function);
			}

			bool Register(uint16_t functionId, const std::string& functionName, CustomSWFFunction function) {
				if (!IsCustom(functionId)) {
					L.Get()->error("Attempted to register invalid custom function ID: {} (must be >= {}).",
						// FIXED: Use static_cast for the enum value
						functionId, static_cast<uint16_t>(HC_SWFFunctionID::CustomBase));
					return false;
				}
				if (!function) {
					return false;
				}
				if (functionName.empty()) {
					return false;
				}

				g_customFunctions[functionId] = function;
				g_customFunctionNames[functionId] = functionName;
				//L.Get()->info("Registered custom SWF function handler: '{}' (ID: {})", functionName, functionId);
				return true;
			}

			bool IsCustom(uint16_t id) {
				// FIXED: Use static_cast for the enum value
				return id >= static_cast<uint16_t>(HC_SWFFunctionID::CustomBase);
			}

			bool HandleCall(uint16_t functionId, void* thisPtr, int swfContext,
				int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn, uint32_t callbackPtr) {
				auto it = g_customFunctions.find(functionId);
				if (it == g_customFunctions.end()) {
					L.Get()->warn("HandleCall: No handler found for custom function ID: {}", functionId);
					return false;
				}

				const std::string& funcName = GetRegisteredName(functionId);
				L.Get()->info("Executing custom SWF function: '{}' (ID: {})", funcName, functionId);

				try {
					it->second(paramCount, swfArgs, swfReturn);

					L.Get()->flush();
					return true;
				}
				catch (const std::exception& e) {
					L.Get()->error("Exception caught in custom function '{}' (ID: {}): {}", funcName, functionId, e.what());
					// FIXED: Call the helper instead of a member function
					Helpers::SWFReturnHelper::SetFailure(swfReturn);
					L.Get()->flush();
					return true;
				}
				catch (...) {
					L.Get()->error("Unknown exception caught in custom function '{}' (ID: {})", funcName, functionId);
					// FIXED: Call the helper instead of a member function
					Helpers::SWFReturnHelper::SetFailure(swfReturn);
					L.Get()->flush();
					return true;
				}
			}

			std::vector<uint16_t> GetRegisteredIds() {
				std::vector<uint16_t> ids;
				ids.reserve(g_customFunctions.size());
				for (const auto& pair : g_customFunctions) {
					ids.push_back(pair.first);
				}
				return ids;
			}

			std::string GetRegisteredName(uint16_t id) {
				auto it = g_customFunctionNames.find(id);
				if (it != g_customFunctionNames.end()) {
					return it->second;
				}
				return "UnknownCustomFunc_" + std::to_string(id);
			}

			static void HelloWorldHandler(int paramCount, HC_SWFArgument** swfArgs, HC_SWFReturn* swfReturn) {
				L.Get()->info("HelloWorld called!");
				L.Get()->flush();
			}

			void InitializeSystem() {
				L.Get()->info("Initializing Custom SWF Functions system...");

				Register(HC_SWFFunctionID::HelloWorld, "HelloWorld", HelloWorldHandler);

				L.Get()->info("Custom SWF Functions system initialized.");
				L.Get()->flush();
			}

			void RegisterAllWithGame(void* gameThisPtr, uintptr_t originalRegisterFuncAddr) {
				//L.Get()->info("Registering custom SWF functions with the game engine...");

				if (!gameThisPtr) {
					L.Get()->error("Cannot register custom functions with game: gameThisPtr is NULL.");
					return;
				}

				int count = 0;
				auto idsToRegister = GetRegisteredIds();

				for (uint16_t id : idsToRegister) {
					std::string name = GetRegisteredName(id);
					try {
						// This function comes from RegisterSWFFunctionHook.h and needs to be correct there too
						Core::DetouredRegisterSWFFunction(gameThisPtr, nullptr, id, name.c_str());
						count++;
					}
					catch (const std::exception& e) {
						L.Get()->error("  std::exception while calling DetouredRegisterSWFFunction for '{}' (ID: {}): {}", name, id, e.what());
					}
					catch (...) {
						L.Get()->error("  Unknown C++ exception while calling DetouredRegisterSWFFunction for '{}' (ID: {})", name, id);
					}
					L.Get()->flush();
				}
				//L.Get()->info("Finished attempting to register {} custom SWF functions with the game engine ({} successful calls logged).", idsToRegister.size(), count);
				L.Get()->flush();
			}
		}
	}
}