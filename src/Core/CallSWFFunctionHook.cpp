#include "CallSWFFunctionHook.h"
#include "../Util/Logger.h"
#include "../SWF/Dispatcher/Dispatcher.h"
#include "../SWF/Data/SWFArgument.h"
#include "../SWF/Data/SWFReturn.h"
#include "../SWF/Helpers/SWFReturnHelper.h" 

namespace HookCrashers {
    namespace Core {

        static Util::Logger& L = Util::Logger::Instance();

        using OriginalCallFunc_t = void(__thiscall*)(
            void* thisPtr,
            int swfContext,
            uint32_t functionIdRaw,
            int paramCount,
            SWF::Data::SWFArgument** swfArgs,
            uint32_t* swfReturnRaw,
            uint32_t callbackPtr
            );
        static OriginalCallFunc_t g_originalFunction = nullptr;

        constexpr uintptr_t CALL_SWF_FUNCTION_OFFSET = 0x7A070;

        void __fastcall DetouredCallSWFFunction(
            void* thisPtr,
            void* /* edx - unused */,
            int swfContext,
            uint32_t functionIdRaw,
            int paramCount,
            SWF::Data::SWFArgument** swfArgs,
            uint32_t* swfReturnRaw,
            uint32_t callbackPtr)
        {
            bool handled = SWF::Dispatcher::Dispatch(
                thisPtr, swfContext, functionIdRaw, paramCount,
                swfArgs, swfReturnRaw, callbackPtr
            );
            if (!handled) {
                if (g_originalFunction) {
                    try {
                        g_originalFunction(thisPtr, swfContext, functionIdRaw, paramCount, swfArgs, swfReturnRaw, callbackPtr);
                    }
                    catch (...) {
                        L.Get()->critical("!!! Exception in original CallSWFFunction !!!");
                        if (swfReturnRaw) SWF::Helpers::SWFReturnHelper::SetFailure(swfReturnRaw);
                    }
                }
                else {
                    L.Get()->error("Original CallSWFFunction pointer is null! Cannot call original for ID {:#x}.", functionIdRaw & 0xFFFF);
                    if (swfReturnRaw) SWF::Helpers::SWFReturnHelper::SetFailure(swfReturnRaw);
                }
            }
            L.Get()->flush();
        }

        bool SetupCallSWFFunctionHook(uintptr_t moduleBase) {
            L.Get()->info("Setting up CallSWFFunction hook...");
            uintptr_t targetAddress = moduleBase + CALL_SWF_FUNCTION_OFFSET;
            g_originalFunction = reinterpret_cast<OriginalCallFunc_t>(targetAddress);

            SWF::Dispatcher::Initialize(targetAddress);


            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            LONG error = DetourAttach(&(PVOID&)g_originalFunction, DetouredCallSWFFunction);
            if (error != NO_ERROR) {
                L.Get()->error("DetourAttach failed for CallSWFFunction: {}", error);
                DetourTransactionAbort();
                g_originalFunction = nullptr;
                SWF::Dispatcher::Initialize(0);
                return false;
            }
            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                L.Get()->error("DetourTransactionCommit failed for CallSWFFunction: {}", error);
                return false;
            }

            L.Get()->info("CallSWFFunction hook attached successfully at 0x{:X}", targetAddress);
            L.Get()->flush();
            return true;
        }

        uintptr_t GetOriginalCallSWFFunctionAddress() {
            return reinterpret_cast<uintptr_t>(g_originalFunction);
        }
    }
}