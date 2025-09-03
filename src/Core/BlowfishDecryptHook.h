#pragma once

#include <vector>
#include <cstdint>

namespace HookCrashers {
    namespace Core {
        bool SetupBlowfishDecryptHook(uintptr_t moduleBase);
        void TeardownBlowfishDecryptHook();
        void StartSaveDataCapture();
        void StopSaveDataCapture();
        void AnalyzeCapturedData();
        const std::vector<uint8_t>& GetCapturedSaveData();
    }
}