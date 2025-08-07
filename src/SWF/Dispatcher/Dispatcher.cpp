#include "Dispatcher.h"
#include "../../Util/Logger.h"
#include "../Custom/CustomFunctions.h"
#include "../Override/Overrides.h"
#include "../Helpers/SWFReturnHelper.h"
#include "../Helpers/SWFArgumentReader.h"

// Define our aliases for the public types
using HC_SWFArgument = HookCrashers::SWF::Data::SWFArgument;
using HC_SWFReturn = HookCrashers::SWF::Data::SWFReturn;

namespace HookCrashers {
	namespace SWF {
		namespace Dispatcher {
			static Util::Logger& L = Util::Logger::Instance();

			// FIXED: Update function pointer type definition
			using OriginalCallSWFFunc_t = void(__thiscall*)(void* thisPtr, int swfContext, uint32_t functionIdRaw, int paramCount,
				HC_SWFArgument** swfArgs, uint32_t* swfReturnRaw, uint32_t callbackPtr);
			static OriginalCallSWFFunc_t g_originalCallSWFFunction = nullptr;

			void Initialize(uintptr_t originalCallSWFFuncAddr) {
				if (originalCallSWFFuncAddr == 0) {
					L.Get()->error("Dispatcher::Initialize: Received null address for original function.");
					return;
				}
				g_originalCallSWFFunction = reinterpret_cast<OriginalCallSWFFunc_t>(originalCallSWFFuncAddr);
				L.Get()->info("Dispatcher initialized with original function pointer: 0x{:X}", originalCallSWFFuncAddr);
			}

			bool Dispatch(
				void* gameThisPtr,
				int swfContext,
				uint32_t functionIdRaw,
				int paramCount,
				HC_SWFArgument** swfArgs,
				uint32_t* swfReturnRaw,
				uint32_t callbackPtr)
			{
				uint16_t functionId = functionIdRaw & 0xFFFF;
				// NOTE: SWFReturnHelper::AsStructured must exist and correctly cast the uint32_t*
				HC_SWFReturn* swfReturn = Helpers::SWFReturnHelper::AsStructured(swfReturnRaw);

				if (Custom::IsCustom(functionId)) {
					L.Get()->info("Dispatching call to Custom Function handler (ID: {})", functionId);
					if (Custom::HandleCall(functionId, gameThisPtr, swfContext, paramCount, swfArgs, swfReturn, callbackPtr)) {
						return true;
					}
					else {
						L.Get()->warn("Custom function (ID: {}) was identified but not handled!", functionId);
						// FIXED: Call the helper. It should handle the SWFReturn* correctly.
						Helpers::SWFReturnHelper::SetFailure(swfReturn);
						return true;
					}
				}

				Override::HookFunction overrideFunc = Override::Get(functionId);
				if (overrideFunc) {
					L.Get()->info("Dispatching call to Override Function handler (ID: {:#x})", functionId);
					try {
						overrideFunc(gameThisPtr, swfContext, functionIdRaw, paramCount, swfArgs, swfReturn, callbackPtr);
						L.Get()->debug("Override function for ID {:#x} executed.", functionId);
						L.Get()->flush();
						return true;
					}
					catch (const std::exception& e) {
						L.Get()->error("Exception caught in override function for ID {:#x}: {}", functionId, e.what());
						// FIXED: Call the helper.
						Helpers::SWFReturnHelper::SetFailure(swfReturn);
						L.Get()->flush();
						return true;
					}
					catch (...) {
						L.Get()->error("Unknown exception caught in override function for ID {:#x}", functionId);
						// FIXED: Call the helper.
						Helpers::SWFReturnHelper::SetFailure(swfReturn);
						L.Get()->flush();
						return true;
					}
				}
				return false;
			}

			void CallOriginal(void* thisPtr, int swfContext, uint32_t functionIdRaw, int paramCount,
				HC_SWFArgument** swfArgs, uint32_t* swfReturnRaw, uint32_t callbackPtr) {
				if (g_originalCallSWFFunction) {
					try {
						g_originalCallSWFFunction(thisPtr, swfContext, functionIdRaw, paramCount, swfArgs, swfReturnRaw, callbackPtr);
					}
					catch (...) {
						L.Get()->error("Unknown exception while calling original CallSWFFunction!");
						// FIXED: Call the helper.
						if (swfReturnRaw) Helpers::SWFReturnHelper::SetFailure(reinterpret_cast<HC_SWFReturn*>(swfReturnRaw));
					}
				}
			}
		}
	}
}