#include "Dispatcher.h"
#include "../../Util/Logger.h"
#include "../Custom/CustomFunctions.h"
#include "../Override/Overrides.h"
#include "../Helpers/SWFReturnHelper.h"
#include "../Helpers/SWFArgumentReader.h"

namespace HookCrashers {
	namespace SWF {
		namespace Dispatcher {
			static Util::Logger& L = Util::Logger::Instance();

			using OriginalCallSWFFunc_t = void(__thiscall*)(void* thisPtr, int swfContext, uint32_t functionIdRaw, int paramCount,
				Data::SWFArgument** swfArgs, uint32_t* swfReturnRaw, uint32_t callbackPtr);
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
				Data::SWFArgument** swfArgs,
				uint32_t* swfReturnRaw,
				uint32_t callbackPtr)
			{
				uint16_t functionId = functionIdRaw & 0xFFFF;
				Data::SWFReturn* swfReturn = Helpers::SWFReturnHelper::AsStructured(swfReturnRaw);

				if (Custom::IsCustom(functionId)) {
					L.Get()->info("Dispatching call to Custom Function handler (ID: {})", functionId);
					if (Custom::HandleCall(functionId, gameThisPtr, swfContext, paramCount, swfArgs, swfReturn, callbackPtr)) {
						return true;
					}
					else {
						L.Get()->warn("Custom function (ID: {}) was identified but not handled!", functionId);
						Helpers::SWFReturnHelper::SetFailure(swfReturnRaw);
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
						Helpers::SWFReturnHelper::SetFailure(swfReturnRaw);
						L.Get()->flush();
						return true;
					}
					catch (...) {
						L.Get()->error("Unknown exception caught in override function for ID {:#x}", functionId);
						Helpers::SWFReturnHelper::SetFailure(swfReturnRaw);
						L.Get()->flush();
						return true;
					}
				}
				return false;
			}

			void CallOriginal(void* thisPtr, int swfContext, uint32_t functionIdRaw, int paramCount,
				Data::SWFArgument** swfArgs, uint32_t* swfReturnRaw, uint32_t callbackPtr) {
				if (g_originalCallSWFFunction) {
					try {
						g_originalCallSWFFunction(thisPtr, swfContext, functionIdRaw, paramCount, swfArgs, swfReturnRaw, callbackPtr);
					}
					catch (...) {
						L.Get()->error("Unknown exception while calling original CallSWFFunction!");
						if (swfReturnRaw) Helpers::SWFReturnHelper::SetFailure(swfReturnRaw);
					}
				}
			}
		}
	}
}