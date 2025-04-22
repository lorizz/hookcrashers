/*#include "NativeFunctions.h"
#include "../Util/Logger.h" // Use the namespaced logger

namespace HookCrashers {
    namespace Native {

        // Define static members
        NativeInfo<void*> GameFunctions::GetFirstArgument = { nullptr, CallingConvention::FastCall }; // Assuming FastCall based on typical SWF interfaces
        NativeInfo<void*> GameFunctions::GetNextArgument = { nullptr, CallingConvention::FastCall };  // Assuming FastCall
        NativeInfo<void*> GameFunctions::SetStorageOffset = { nullptr, CallingConvention::ThisCall }; // Based on Ghidra SetStorageOffset(this, i, off)
        NativeInfo<void*> GameFunctions::ReadStorageData = { nullptr, CallingConvention::ThisCall };  // Based on Ghidra Native_ReadStorage(this, i, &out)
        NativeInfo<void*> GameFunctions::WriteStorageData = { nullptr, CallingConvention::ThisCall }; // Based on Ghidra Native_WriteStorage(this, i, val)

        // Macro to simplify logging function loading
        #define LOG_NATIVE_LOAD(funcInfo, funcName, baseAddr) \
            if (funcInfo.IsValid()) { \
                Util::Logger::Instance().Get()->info("Loaded native function: {} at 0x{:X}", \
                    funcName, funcInfo.GetAddressValue()); \
            } else { \
                Util::Logger::Instance().Get()->error("Failed to load native function: {}", funcName); \
            }


        bool LoadNatives(uintptr_t moduleBase) {
            Util::Logger::Instance().Get()->info("Loading native function addresses (Base: 0x{:X})...", moduleBase);

            // Populate addresses - **VERIFY THESE OFFSETS**
            GameFunctions::GetFirstArgument.Address = reinterpret_cast<void*>(moduleBase + 0x74520); // FUN_00bf4520 ? Need to verify what Ghidra `GetFirstArgument` points to
            GameFunctions::GetNextArgument.Address = reinterpret_cast<void*>(moduleBase + 0x74140);  // FUN_00bf4140 ? Need to verify Ghidra `GetNextArgument`
            GameFunctions::SetStorageOffset.Address = reinterpret_cast<void*>(moduleBase + 0x4AF10);   // FUN_00bdaf10 - Seems plausible
            GameFunctions::ReadStorageData.Address = reinterpret_cast<void*>(moduleBase + 0x4AE40);    // FUN_00bdae40 - Seems plausible (Native_ReadStorage)
            GameFunctions::WriteStorageData.Address = reinterpret_cast<void*>(moduleBase + 0x4AD70);   // Guessed address for Native_WriteStorage - **NEEDS VERIFICATION**

            // Log loaded functions
            LOG_NATIVE_LOAD(GameFunctions::GetFirstArgument, "GetFirstArgument", moduleBase);
            LOG_NATIVE_LOAD(GameFunctions::GetNextArgument, "GetNextArgument", moduleBase);
            LOG_NATIVE_LOAD(GameFunctions::SetStorageOffset, "SetStorageOffset", moduleBase);
            LOG_NATIVE_LOAD(GameFunctions::ReadStorageData, "ReadStorageData", moduleBase);
            LOG_NATIVE_LOAD(GameFunctions::WriteStorageData, "WriteStorageData", moduleBase);

            Util::Logger::Instance().Get()->flush();

            // Check if all essential functions were loaded (optional)
            return GameFunctions::GetFirstArgument.IsValid() &&
                GameFunctions::GetNextArgument.IsValid() &&
                GameFunctions::SetStorageOffset.IsValid() &&
                GameFunctions::ReadStorageData.IsValid() &&
                GameFunctions::WriteStorageData.IsValid(); // Add others if critical
        }


        // --- String ID Lookup Placeholder ---

        // !! IMPORTANT !!
        // Replace this function once you find the actual string lookup function in the game.
        // You'll need its address and signature (calling convention, parameters, return type).
        const char* GetGameStringById(uint16_t stringId) {
            // Placeholder implementation
            static char buffer[128]; // Static buffer for simplicity, be mindful of multi-threading if used extensively

            // TODO: Reverse engineer the game to find the real function!
            // Example structure if you found it:
            /*
            uintptr_t moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandle(NULL)); // Or get from logger/manager
            uintptr_t lookupFuncAddr = moduleBase + 0xYYYYYY; // Replace YYYYYY with the offset

            if (lookupFuncAddr != moduleBase) { // Basic check if address is non-zero
                // Define the function signature based on RE findings
                typedef const char* (__cdecl *GameGetString_t)(uint16_t id); // Example: cdecl, takes ID, returns char*
                GameGetString_t lookupFunc = reinterpret_cast<GameGetString_t>(lookupFuncAddr);

                try {
                    const char* result = lookupFunc(stringId);
                    if (result) {
                        // Optional: Copy to buffer if the game pointer might become invalid,
                        // but returning the direct pointer is usually fine and more efficient.
                        // strncpy_s(buffer, sizeof(buffer), result, _TRUNCATE);
                        // return buffer;
                        return result; // Return game's pointer directly
                    } else {
                         snprintf(buffer, sizeof(buffer), "StringID:%u (Lookup Failed)", stringId);
                         return buffer;
                    }
                } catch(...) {
                     snprintf(buffer, sizeof(buffer), "StringID:%u (Lookup Exception)", stringId);
                     return buffer;
                }
            }
            +/

            // Fallback placeholder if lookup not found/implemented
            snprintf(buffer, sizeof(buffer), "StringID:%u (Lookup Missing)", stringId);
            return buffer;
        }


    } // namespace Native
} // namespace HookCrashers*/