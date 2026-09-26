#include "SWFStringTableExpansion.h"
#include "../Core/Logger.h"
#include "../Core/Memory.h"
#include <cstdint>
#include <cstring>
namespace HookCrashers::SWF::SWFStringTableExpansion {
namespace {
constexpr std::uint32_t kStringArenaSize = 0x100000;
constexpr std::uint32_t kStringPointerCount = 0x10000;
constexpr std::uint32_t kStringPointerBytes = kStringPointerCount * sizeof(std::uint32_t);
constexpr std::uint32_t kHashEntryCount = 0x10000;
constexpr std::uint32_t kHashTableBytes = kHashEntryCount * sizeof(std::uint16_t);
struct CapacityPatch {
    std::uintptr_t rva;
    std::uint32_t expected;
    std::uint32_t replacement;
    const char* name;
};
constexpr CapacityPatch kPatches[] {
    { 0x1216C3, 0x20000, kStringArenaSize, "string arena allocation" },
    { 0x121714, 0x8000, kStringPointerBytes, "string pointer allocation" },
    { 0x121765, 0x8000, kHashTableBytes, "string hash allocation" },
    { 0x1217AD, 0x20000, kStringArenaSize, "string arena capacity" },
    { 0x1217B7, 0x2000, kStringPointerCount, "string pointer capacity" },
    { 0x1217C1, 0x4000, kHashEntryCount, "string hash capacity" },
};
bool ApplyCapacityPatch(const CapacityPatch& patch) {
    std::uint32_t current = 0;
    std::memcpy(&current, reinterpret_cast<const void*>(Core::Memory::Va(patch.rva)), sizeof(current));
    if (current == patch.replacement) {
        return true;
    }
    if (current != patch.expected) {
        Core::Logger::Error("SWF string table patch mismatch name='%s' rva=0x%08X expected=0x%08X current=0x%08X",
                            patch.name,
                            static_cast<unsigned int>(patch.rva),
                            patch.expected,
                            current);
        return false;
    }
    return Core::Memory::WriteU32(patch.rva, patch.replacement);
}
} // namespace
bool Install() {
    for (const CapacityPatch& patch : kPatches) {
        if (!ApplyCapacityPatch(patch)) {
            Core::Logger::Error("SWF string table expansion failed");
            return false;
        }
    }
    Core::Logger::Info("SWF string table expanded arenaBytes=%u pointerSlots=%u hashSlots=%u",
                       kStringArenaSize,
                       kStringPointerCount,
                       kHashEntryCount);
    return true;
}
} // namespace HookCrashers::SWF::SWFStringTableExpansion