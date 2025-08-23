#include "IsFeatureEnabledHook.h"
#include "../Util/Logger.h"

namespace HookCrashers {
    namespace Core {
        static Util::Logger& L = Util::Logger::Instance();

        using OriginalIsFeatureEnabledFunc_t = uint32_t(__thiscall*)(void* thisPtr, uint16_t featureId );
        static OriginalIsFeatureEnabledFunc_t g_originalFunction = nullptr;

        static void* g_correctObjectManagerPtr = nullptr;

        constexpr uintptr_t IS_FEATURE_ENABLED_OFFSET = 0x38070;

        uint32_t __fastcall DetouredIsFeatureEnabled(void* thisPtr, void* /* edx - unused dummy */, uint16_t featureId) {
            if (thisPtr != nullptr) {
                static void* lastLoggedPtr = nullptr;
                if (lastLoggedPtr != thisPtr) {
                    g_correctObjectManagerPtr = thisPtr;
                    lastLoggedPtr = g_correctObjectManagerPtr;
                }
                else {
                    g_correctObjectManagerPtr = thisPtr;
                }
            }
            if (g_originalFunction) {
                try {
                    return g_originalFunction(thisPtr, featureId);
                }
                catch (const std::exception& e) {
                }
                catch (...) {
                }
            }
        }

        uint32_t GetIsFeatureEnabled(uint16_t featureId) {
            if (g_correctObjectManagerPtr != nullptr) {
				uint32_t isEnabled = DetouredIsFeatureEnabled(g_correctObjectManagerPtr, nullptr, featureId);
                return isEnabled;
            }
            return 0;
        }

        bool SetupIsFeatureEnabledHook(uintptr_t moduleBase) {
            uintptr_t targetAddress = moduleBase + IS_FEATURE_ENABLED_OFFSET;
            g_originalFunction = reinterpret_cast<OriginalIsFeatureEnabledFunc_t>(targetAddress);

            if (!g_originalFunction) {
                return false;
            }

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            LONG error = DetourAttach(&(PVOID&)g_originalFunction, DetouredIsFeatureEnabled);
            if (error != NO_ERROR) {
                DetourTransactionAbort();
                g_originalFunction = nullptr;
                return false;
            }
            error = DetourTransactionCommit();
            if (error != NO_ERROR) {
                g_originalFunction = nullptr;
                return false;
            }
            return true;
        }

        void* GetCorrectObjectManagerPtr() {
            return g_correctObjectManagerPtr;
        }
    }
}