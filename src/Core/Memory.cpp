#include "Memory.h"

#include "../NativeDB/Natives.h"
#include <windows.h>
#include <cstring>

namespace HookCrashers::Core::Memory {

std::uintptr_t GameBase() {
    return HookCrashers::NativeDB::GetGameBase();
}

std::uintptr_t Va(std::uintptr_t rva) {
    return GameBase() + rva;
}

bool Write(std::uintptr_t rva, const void* data, std::size_t size) {
    void* address = reinterpret_cast<void*>(Va(rva));
    DWORD oldProtect = 0;
    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    std::memcpy(address, data, size);
    FlushInstructionCache(GetCurrentProcess(), address, size);

    DWORD ignored = 0;
    VirtualProtect(address, size, oldProtect, &ignored);
    return true;
}

bool WriteBytes(std::uintptr_t rva, const std::vector<std::uint8_t>& bytes) {
    return Write(rva, bytes.data(), bytes.size());
}

bool WriteU8(std::uintptr_t rva, int value) {
    const auto v = static_cast<std::uint8_t>(value);
    return Write(rva, &v, sizeof(v));
}

bool WriteU16(std::uintptr_t rva, int value) {
    const auto v = static_cast<std::uint16_t>(value);
    return Write(rva, &v, sizeof(v));
}

bool WriteU32(std::uintptr_t rva, std::uint32_t value) {
    return Write(rva, &value, sizeof(value));
}

} // namespace HookCrashers::Core::Memory
