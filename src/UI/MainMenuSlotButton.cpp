#include "MainMenuSlotButton.h"

#include "MenuApi.h"
#include "MenuNative.h"
#include "../Core/Logger.h"
#include "../Core/Memory.h"
#include "../Game/MenuTable.h"
#include "../SWF/SaveSlotButtonAsset.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>

// RVAs of castle.exe build 0x6A4D42BF; see re-archive/analysis/decomp-mainmenu.
namespace HookCrashers::UI::MainMenuSlotButton {
namespace {

constexpr std::uintptr_t kMainPreUpdateRva = 0x146360;     // cdecl u8(owner): row navigation of items 3/4 and 5/6
constexpr std::uintptr_t kInvokeCurrentItemRva = 0x1467A0; // cdecl(owner): hotspot click, confirms the selection
constexpr std::uintptr_t kPressedRightRva = 0x151710;      // thiscall u8(state, player), ret 4
constexpr std::uintptr_t kPressedLeftRva = 0x151560;
constexpr std::uintptr_t kPressedDownRva = 0x151200;
constexpr std::uintptr_t kPressedUpRva = 0x1513B0;
constexpr std::uintptr_t kMenuMoveSoundRva = 0x0C9DF0;     // stdcall(0, 0), xmm3 = 0

constexpr std::uint16_t kMainMenuId = 23;
constexpr std::size_t kStateItemCount = 0x04, kStateHotspotCount = 0x0A, kStateSelected = 0x0C;
constexpr std::size_t kStateHotspots = 0x1C, kStateItems = 0x44, kStatePreUpdate = 0x1D4;
constexpr std::size_t kMainCurrentHandler = 0x38, kMainTable = 0x40;
constexpr std::size_t kItemColor = 0x10, kItemFallbackColor = 0x14;
constexpr std::uint32_t kSelectedColor = 0xFFFF00FFu, kIdleColor = 0x777777FFu;
constexpr std::size_t kHotspotSize = 0x34;
constexpr unsigned kMaxHotspots = 10;
constexpr std::uint16_t kGreen = 3, kRed = 4, kSlots = 7;
constexpr int kAnyPlayer = -2;

using PressedFn = std::uint8_t(__thiscall*)(void* state, int player);
using PreUpdateFn = std::uint8_t(__cdecl*)(void* owner);

PreUpdateFn g_originalPreUpdate = nullptr;
void* g_mainState = nullptr;

template <typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

bool Pressed(std::uintptr_t rva, void* state) {
    return reinterpret_cast<PressedFn>(Core::Memory::Va(rva))(state, kAnyPlayer) != 0;
}

void PlayMoveSound() {
    const auto fn = Core::Memory::Va(kMenuMoveSoundRva);
    __asm {
        xorps xmm3, xmm3
        push 0
        push 0
        call fn
    }
}

void* ItemAt(void* state, std::uint16_t index) {
    return Field<void*>(state, kStateItems + index * sizeof(void*));
}

// Same bookkeeping as the native CC_MenuBuilder_SelectTab* callbacks.
void Select(void* state, std::uint16_t index) {
    const std::uint16_t current = Field<std::uint16_t>(state, kStateSelected);
    if (current == index) return;
    if (void* old = ItemAt(state, current)) {
        const std::uint32_t fallback = Field<std::uint32_t>(old, kItemFallbackColor);
        Field<std::uint32_t>(old, kItemColor) = fallback ? fallback : kIdleColor;
    }
    Field<std::uint16_t>(state, kStateSelected) = index;
    if (void* item = ItemAt(state, index)) Field<std::uint32_t>(item, kItemColor) = kSelectedColor;
}

std::uint8_t __cdecl PreUpdate(void* owner) {
    void* state = owner ? Field<void*>(owner, kMainCurrentHandler) : nullptr;
    if (state && state == g_mainState) {
        const std::uint16_t selected = Field<std::uint16_t>(state, kStateSelected);
        if (selected == kGreen || selected == kRed || selected == kSlots) {
            int target = -1;
            if (Pressed(kPressedRightRva, state)) {
                target = selected == kGreen ? kRed : selected == kRed ? kSlots : kGreen;
            } else if (Pressed(kPressedLeftRva, state)) {
                target = selected == kGreen ? kSlots : selected == kRed ? kGreen : kRed;
            } else if (selected == kSlots && Pressed(kPressedDownRva, state)) {
                target = 5;
            } else if (selected == kSlots && Pressed(kPressedUpRva, state)) {
                target = 2;
            }
            if (target >= 0) {
                Select(state, static_cast<std::uint16_t>(target));
                PlayMoveSound();
                return 1;
            }
            if (selected == kSlots) return 0; // confirm/back: native handling
        }
    }
    return g_originalPreUpdate ? g_originalPreUpdate(owner) : 0;
}

void __cdecl HoverSlots(void* owner) {
    void* state = owner ? Field<void*>(owner, kMainCurrentHandler) : nullptr;
    if (state && state == g_mainState && Field<std::uint16_t>(state, kStateSelected) != kSlots) {
        Select(state, kSlots);
        PlayMoveSound();
    }
}

void __cdecl ActivateSlots(void* owner) {
    (void)owner;
    const auto result = Menus::Api::Open("hc.saveslots");
    Core::Logger::Info("Main menu: save slots button -> %s", Menus::Api::OpenResultName(result));
}

bool Near(float value, float expected) {
    return std::fabs(value - expected) < 0.01f;
}

} // namespace

void OnMainMenuBuilt(void* mainMenu) {
    g_mainState = nullptr;
    if (!mainMenu || !SWF::SaveSlotButtonAsset::IsPatched() || !Menus::Native::IsAvailable()) return;
    void* table = Field<void*>(mainMenu, kMainTable);
    if (!table) return;
    void* state = static_cast<std::uint8_t*>(table) + kMainMenuId * Game::MenuTable::kHandlerSize;

    // Only touch the menu when it is exactly the stock one.
    const auto preUpdate = reinterpret_cast<PreUpdateFn>(Core::Memory::Va(kMainPreUpdateRva));
    const std::uint16_t items = Field<std::uint16_t>(state, kStateItemCount);
    const std::uint16_t hotspots = Field<std::uint16_t>(state, kStateHotspotCount);
    if (Field<PreUpdateFn>(state, kStatePreUpdate) != preUpdate || items != 7 || hotspots != 7 ||
        hotspots >= kMaxHotspots) {
        Core::Logger::Warn("Main menu: unexpected layout (items=%u hotspots=%u); save slots button not added",
                           items, hotspots);
        return;
    }
    auto* green = Field<float*>(state, kStateHotspots + 3 * sizeof(void*));
    auto* red = Field<float*>(state, kStateHotspots + 4 * sizeof(void*));
    if (!green || !red || !Near(green[0], 25.5f) || !Near(green[2], 187.5f) || !Near(red[0], 198.5f) ||
        !Near(red[2], 279.5f) || !Near(green[1], red[1]) || !Near(green[3], red[3])) {
        Core::Logger::Warn("Main menu: row hotspots differ from the supported build; save slots button not added");
        return;
    }

    void* item = Menus::Native::AddHiddenTextItem(state, mainMenu, reinterpret_cast<void*>(&ActivateSlots));
    auto* hotspot = static_cast<std::uint8_t*>(Menus::Native::AllocateGameMemory(kHotspotSize));
    if (!item || !hotspot || Field<std::uint16_t>(state, kStateItemCount) != kSlots + 1) {
        Core::Logger::Error("Main menu: could not add the save slots item");
        return;
    }

    const auto layout = SWF::SaveSlotButtonAsset::Layout();
    std::memset(hotspot, 0, kHotspotSize);
    auto* rect = reinterpret_cast<float*>(hotspot);
    rect[0] = layout.slotsLeft;
    rect[1] = red[1];
    rect[2] = layout.slotsRight;
    rect[3] = red[3];
    Field<void*>(hotspot, 0x1C) = mainMenu;
    Field<void*>(hotspot, 0x20) = reinterpret_cast<void*>(&HoverSlots);
    Field<void*>(hotspot, 0x28) = reinterpret_cast<void*>(Core::Memory::Va(kInvokeCurrentItemRva));
    hotspot[0x30] |= 1;
    Field<void*>(state, kStateHotspots + hotspots * sizeof(void*)) = hotspot;
    Field<std::uint16_t>(state, kStateHotspotCount) = static_cast<std::uint16_t>(hotspots + 1);

    green[2] = layout.greenRight;
    red[0] = layout.redLeft;
    red[2] = layout.redRight;

    g_originalPreUpdate = preUpdate;
    Field<PreUpdateFn>(state, kStatePreUpdate) = &PreUpdate;
    g_mainState = state;
    Core::Logger::Info("Main menu: save slots button added (item %u, hotspot %u)", kSlots, hotspots);
}

} // namespace HookCrashers::UI::MainMenuSlotButton
