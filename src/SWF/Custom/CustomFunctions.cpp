#include "CustomFunctions.h"
#include "../../Util/Logger.h"
#include "../Helpers/SWFArgumentReader.h"
#include "../Helpers/SWFReturnHelper.h"
#include <unordered_map>
#include <stdexcept>

#include "../../Core/RegisterSWFFunctionHook.h"

namespace HookCrashers {
	namespace SWF {
		namespace Custom {
			static Util::Logger& L = Util::Logger::Instance();
			static std::unordered_map<uint16_t, CustomSWFFunction> g_customFunctions;
			static std::unordered_map<uint16_t, std::string> g_customFunctionNames;

			void HelloWorldHandler(int paramCount, Data::SWFArgument** swfArgs, Data::SWFReturn* swfReturn) {
				L.Get()->info("Custom::HelloWorldHandler called with {} parameters.", paramCount);

				if (paramCount < 1) {
					L.Get()->warn("HelloWorldHandler called with 0 parameters, expected at least 1 (string).");
					swfReturn->SetFailure();
					return;
				}

				std::string message = Helpers::SWFArgumentReader::GetValueAsString(swfArgs[0]);
				L.Get()->info("HelloWorld Param 0: {}", message);

				swfReturn->SetBooleanSuccess(true);
				L.Get()->flush();
			}

			bool Register(Data::SWFFunctionID functionId, const std::string& functionName, CustomSWFFunction function) {
				return Register(Data::ToValue(functionId), functionName, function);
			}

			bool Register(uint16_t functionId, const std::string& functionName, CustomSWFFunction function) {
				if (!IsCustom(functionId)) {
					L.Get()->error("Attempted to register invalid custom function ID: {} (must be >= {}).",
						functionId, Data::ToValue(Data::SWFFunctionID::CustomBase));
					return false;
				}
				if (!function) {
					L.Get()->warn("Attempted to register a null custom function for ID: {}", functionId);
					return false;
				}
				if (functionName.empty()) {
					L.Get()->warn("Attempted to register custom function ID {} with an empty name.", functionId);
					return false;
				}

				g_customFunctions[functionId] = function;
				g_customFunctionNames[functionId] = functionName;
				L.Get()->info("Registered custom SWF function handler: '{}' (ID: {})", functionName, functionId);
				return true;
			}

			bool IsCustom(uint16_t id) {
				return id >= Data::ToValue(Data::SWFFunctionID::CustomBase);
			}

			bool HandleCall(uint16_t functionId, void* thisPtr, int swfContext,
				int paramCount, Data::SWFArgument** swfArgs, Data::SWFReturn* swfReturn, uint32_t callbackPtr) {
				auto it = g_customFunctions.find(functionId);
				if (it == g_customFunctions.end()) {
					L.Get()->warn("HandleCall: No handler found for custom function ID: {}", functionId);
					return false;
				}

				const std::string& funcName = GetRegisteredName(functionId);
				L.Get()->info("Executing custom SWF function: '{}' (ID: {})", funcName, functionId);

				if (paramCount > 0) {
					L.Get()->debug("  Arguments:");
					for (int i = 0; i < paramCount; ++i) {
						L.Get()->debug("    [{}]: {}", i, Helpers::SWFArgumentReader::GetValueAsString(swfArgs[i]));
					}
				}

				try {
					// Call the registered std::function
					it->second(paramCount, swfArgs, swfReturn);

					L.Get()->debug("Custom function '{}' executed.", funcName);
					L.Get()->flush();
					return true; // Indicate handled successfully
				}
				catch (const std::exception& e) {
					L.Get()->error("Exception caught in custom function '{}' (ID: {}): {}", funcName, functionId, e.what());
					if (swfReturn) {
						swfReturn->SetFailure(); // Set failure on exception
					}
					L.Get()->flush();
					return true; // Indicate handled (even though it failed) to prevent calling original
				}
				catch (...) {
					L.Get()->error("Unknown exception caught in custom function '{}' (ID: {})", funcName, functionId);
					if (swfReturn) {
						swfReturn->SetFailure();
					}
					L.Get()->flush();
					return true; // Indicate handled
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
				return "UnknownCustomFunc_" + std::to_string(id); // Fallback name
			}


			void InitializeSystem() {
				L.Get()->info("Initializing Custom SWF Functions system...");

				// Register built-in custom functions here using the named registration
				Register(Data::ToValue(Data::SWFFunctionID::HelloWorld), "HelloWorld", HelloWorldHandler);
				// Register(...) other custom functions

				L.Get()->info("Custom SWF Functions system initialized.");
				L.Get()->flush();
			}

			void RegisterAllWithGame(void* gameThisPtr, uintptr_t originalRegisterFuncAddr) {
				L.Get()->info("Registering custom SWF functions with the game engine...");

				if (!gameThisPtr) {
					L.Get()->error("Cannot register custom functions with game: gameThisPtr is NULL.");
					return;
				}

				int count = 0;
				auto idsToRegister = GetRegisteredIds();
				L.Get()->debug("Found {} custom function IDs to register.", idsToRegister.size());
				
				for (uint16_t id : idsToRegister) {
					L.Get()->debug("--- Loop iteration: Processing ID value = {} (0x{:X})", id, id); // <-- AGGIUNGI QUESTO LOG
					std::string name = GetRegisteredName(id);
					L.Get()->debug("--> Attempting to register with game: '{}' (ID: {}) using thisPtr 0x:{X}", name, id,
						reinterpret_cast<uintptr_t>(gameThisPtr));

					try {
						Core::DetouredRegisterSWFFunction(gameThisPtr, nullptr, id, name.c_str());
						L.Get()->debug("<-- Successfully called DetouredRegisterSWFFunction for '{}' (ID: {})", name, id);
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
				L.Get()->info("Finished attempting to register {} custom SWF functions with the game engine ({} successful calls logged).", idsToRegister.size(), count);
				L.Get()->flush();
			}
		}
	}
}