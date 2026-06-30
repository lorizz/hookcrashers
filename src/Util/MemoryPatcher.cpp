#include "MemoryPatcher.h"
#include <Windows.h>
#include "Logger.h"
#include "../../include/HookCrashers/Public/Globals.h"

namespace HookCrashers {
    namespace Util {
        bool MemoryPatcher::UnprotectMemory(uintptr_t address, size_t size, uint32_t& oldProtect) {
            return VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, reinterpret_cast<PDWORD>(&oldProtect));
        }

        void MemoryPatcher::ReprotectMemory(uintptr_t address, size_t size, uint32_t oldProtect) {
            VirtualProtect(reinterpret_cast<void*>(address), size, oldProtect, reinterpret_cast<PDWORD>(&oldProtect));
        }

        bool MemoryPatcher::PatchBytes(uintptr_t offset, const std::vector<uint8_t>& newBytes) {
            Logger& L = Logger::Instance();

            if (g_moduleBase == 0) {
                L.Get()->critical("[Patch] Cannot patch memory: module base is not initialized.");
                return false;
            }

            if (newBytes.empty()) {
                L.Get()->warn("[Patch] Refusing empty patch | RVA=0x{:X} | VA=0x{:X}.", offset, g_moduleBase + offset);
                return false;
            }

            const uintptr_t absoluteAddress = g_moduleBase + offset;
            uint32_t oldProtect = 0;
            if (!UnprotectMemory(absoluteAddress, newBytes.size(), oldProtect)) {
                L.Get()->error(
                    "[Patch] VirtualProtect failed before write | RVA=0x{:X} | VA=0x{:X} | bytes={} | GetLastError={}.",
                    offset,
                    absoluteAddress,
                    newBytes.size(),
                    GetLastError());
                return false;
            }

            memcpy(reinterpret_cast<void*>(absoluteAddress), newBytes.data(), newBytes.size());

            ReprotectMemory(absoluteAddress, newBytes.size(), oldProtect);
            return true;
        }
    }
}