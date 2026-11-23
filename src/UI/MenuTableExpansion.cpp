#include "MenuTableExpansion.h"

#include "../Core/Logger.h"
#include "../Core/Memory.h"
#include "../Game/MenuTable.h"

#include <cstdint>

namespace HookCrashers::UI::MenuTableExpansion {
namespace {

// Inside the menu builder (RVA 0x1442F0), build 2026-07-07:
//   0x1443E0  68 2C 5E 00 00        push 0x5E2C          ; 4 + 46 * 0x20C
//   0x144413  6A 2E                 push 0x2E            ; count passed to the array ctor
//   0x144415  C7 07 2E 00 00 00     mov dword [edi], 0x2E ; count stored before the array
constexpr std::uintptr_t kAllocationPushRva = 0x1443E0;
constexpr std::uintptr_t kAllocationSizeRva = 0x1443E1;
constexpr std::uintptr_t kCountPushRva = 0x144413;
constexpr std::uintptr_t kCountPushValueRva = 0x144414;
constexpr std::uintptr_t kCountStoreRva = 0x144415;
constexpr std::uintptr_t kCountStoreValueRva = 0x144417;

constexpr std::uint32_t AllocationSize(std::uint32_t count) {
    return 4 + count * static_cast<std::uint32_t>(Game::MenuTable::kHandlerSize);
}

bool g_installed = false;

template <typename T>
T Read(std::uintptr_t rva) {
    return *reinterpret_cast<const T*>(Core::Memory::Va(rva));
}

bool HasCount(std::uint32_t count) {
    return Read<std::uint8_t>(kAllocationPushRva) == 0x68 &&
           Read<std::uint32_t>(kAllocationSizeRva) == AllocationSize(count) &&
           Read<std::uint8_t>(kCountPushRva) == 0x6A &&
           Read<std::uint8_t>(kCountPushValueRva) == count &&
           Read<std::uint16_t>(kCountStoreRva) == 0x07C7 &&
           Read<std::uint32_t>(kCountStoreValueRva) == count;
}

bool WriteCount(std::uint32_t count) {
    return Core::Memory::WriteU32(kAllocationSizeRva, AllocationSize(count)) &&
           Core::Memory::WriteU8(kCountPushValueRva, static_cast<int>(count)) &&
           Core::Memory::WriteU32(kCountStoreValueRva, count);
}

} // namespace

bool Install() {
    if (g_installed) {
        return true;
    }
    if (HasCount(Game::MenuTable::kTotalCount)) {
        g_installed = true;
        return true;
    }
    if (!HasCount(Game::MenuTable::kVanillaCount)) {
        Core::Logger::Error("MenuTable: menu builder bytes do not match the supported build; table not expanded");
        return false;
    }
    if (!WriteCount(Game::MenuTable::kTotalCount)) {
        WriteCount(Game::MenuTable::kVanillaCount);
        Core::Logger::Error("MenuTable: failed to patch the menu builder; restored vanilla count");
        return false;
    }
    g_installed = true;
    Core::Logger::Info("MenuTable: handler table expanded %u -> %u (mod menu slots %u..%u)",
                       Game::MenuTable::kVanillaCount,
                       Game::MenuTable::kTotalCount,
                       Game::MenuTable::kFirstModMenuId,
                       Game::MenuTable::kTotalCount - 1);
    return true;
}

bool IsInstalled() {
    return g_installed;
}

void Remove() {
    if (!g_installed) {
        return;
    }
    WriteCount(Game::MenuTable::kVanillaCount);
    g_installed = false;
}

} // namespace HookCrashers::UI::MenuTableExpansion
