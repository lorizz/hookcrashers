#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace HookCrashers::Core::Memory {

std::uintptr_t GameBase();
std::uintptr_t Va(std::uintptr_t rva);
bool Write(std::uintptr_t rva, const void* data, std::size_t size);
bool WriteBytes(std::uintptr_t rva, const std::vector<std::uint8_t>& bytes);
bool WriteU8(std::uintptr_t rva, int value);
bool WriteU16(std::uintptr_t rva, int value);
bool WriteU32(std::uintptr_t rva, std::uint32_t value);

} // namespace HookCrashers::Core::Memory
