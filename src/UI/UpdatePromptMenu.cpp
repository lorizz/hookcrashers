#include "UpdatePromptMenu.h"

#include "../Core/Logger.h"
#include "../Core/Memory.h"
#include "../Updater/AutoUpdater.h"

#include <windows.h>
#include <detours.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace HookCrashers::UI::UpdatePromptMenu {
namespace {

constexpr std::uintptr_t kMainMenuBuilderRva = 0x1442F0;
constexpr std::uintptr_t kSetMenuStateRva = 0x12C4C0;
constexpr std::uintptr_t kGetAllocatorRva = 0x0F6AC0;
constexpr std::uintptr_t kAllocateRva = 0x0DFE70;
constexpr std::uintptr_t kFreeRva = 0x0F6DF0;
constexpr std::uintptr_t kAllocatorReadyByteRva = 0x360EE1;
constexpr std::uintptr_t kAllocatorCriticalSectionRva = 0x361118;

constexpr std::size_t kCurrentMenuOffset = 0x38;
constexpr std::size_t kActiveMenuIdOffset = 0x3C;
constexpr std::size_t kMenuTableOffset = 0x40;
constexpr std::size_t kMenuStateSize = 0x20C;
constexpr std::size_t kMenuItemCountOffset = 0x04;
constexpr std::size_t kSelectedIndexOffset = 0x0C;
constexpr std::size_t kMenuItemsOffset = 0x44;
constexpr std::size_t kBackCallbackOffset = 0x1B4;
constexpr std::size_t kConfiguredTargetOffset = 0x1DA;

constexpr std::size_t kTextCallbackOffset = 0x38;
constexpr std::size_t kTextBufferOffset = 0x3C;
constexpr std::size_t kTextScaleOffset = 0x44;
constexpr std::size_t kTextLengthOffset = 0x4A;
constexpr std::size_t kTextVisualWidthOffset = 0x4C;
constexpr std::size_t kTextMaxWidthOffset = 0x4E;

constexpr std::uint16_t kConfirmationMenuId = 3;
constexpr std::uint16_t kMainMenuId = 23;

using MainMenuBuilderFn = void*(__thiscall*)(void* self, void* scene, int flags);
using SetMenuStateFn = void(__thiscall*)(void* self, std::uint16_t menuId, void* menuState);
using GetAllocatorFn = void*(__cdecl*)();
using AllocateFn = void*(__thiscall*)(void* allocator, int size);
using FreeFn = void(__cdecl*)(void* allocation);
using MenuCallbackFn = void(__cdecl*)(void* mainMenu);

MainMenuBuilderFn g_originalMainMenuBuilder = nullptr;
SetMenuStateFn g_setMenuState = nullptr;
GetAllocatorFn g_getAllocator = nullptr;
AllocateFn g_allocate = nullptr;
FreeFn g_free = nullptr;
bool g_installed = false;
bool g_promptShown = false;
std::atomic<bool> g_updateCheckComplete { false };
std::atomic<bool> g_startupUpdateAvailable { false };
void* g_mainMenu = nullptr;
bool g_apiPromptPending = false;
bool g_apiPromptActive = false;
std::wstring g_apiPromptMessage;
std::function<void()> g_apiOnConfirm;
std::function<void()> g_apiOnDecline;

bool TryOpenPendingApiPrompt(void* mainMenu);

template <typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

void* AllocateGameMemory(std::size_t size) {
    if (!g_getAllocator || !g_allocate ||
        *reinterpret_cast<const std::uint8_t*>(Core::Memory::Va(kAllocatorReadyByteRva)) == 0) {
        return nullptr;
    }

    auto* criticalSection = reinterpret_cast<LPCRITICAL_SECTION>(
        Core::Memory::Va(kAllocatorCriticalSectionRva));
    EnterCriticalSection(criticalSection);
    void* allocator = g_getAllocator();
    void* allocation = allocator ? g_allocate(allocator, static_cast<int>(size)) : nullptr;
    LeaveCriticalSection(criticalSection);
    return allocation;
}

std::uint16_t VisualWidth(const wchar_t* text) {
    unsigned widthUnits = 0;
    for (const wchar_t* cursor = text; *cursor; ++cursor) {
        widthUnits += *cursor < 0x80 ? 8u : 11u;
    }
    return static_cast<std::uint16_t>((std::min)(widthUnits >> 3, 0xFFFFu));
}

bool SetRawText(void* item, const wchar_t* text, std::uint16_t maxWidth = 0) {
    if (!item || !text) {
        return false;
    }

    const std::size_t length = std::wcslen(text);
    if (length > 0xFFFFu) {
        return false;
    }

    const std::size_t bytes = (length + 1) * sizeof(wchar_t);
    auto* replacement = static_cast<wchar_t*>(AllocateGameMemory(bytes));
    if (!replacement) {
        return false;
    }
    std::memcpy(replacement, text, bytes);

    void*& oldText = Field<void*>(item, kTextBufferOffset);
    if (oldText && g_free) {
        g_free(oldText);
    }
    oldText = replacement;

    const std::uint16_t visualWidth = VisualWidth(text);
    Field<std::uint16_t>(item, kTextLengthOffset) = static_cast<std::uint16_t>(length);
    Field<std::uint16_t>(item, kTextVisualWidthOffset) = visualWidth;
    if (maxWidth != 0) {
        Field<std::uint16_t>(item, kTextMaxWidthOffset) = maxWidth;
    }

    float scale = 1.0f;
    const std::uint16_t effectiveMaxWidth = Field<std::uint16_t>(item, kTextMaxWidthOffset);
    if (effectiveMaxWidth != 0 && visualWidth > effectiveMaxWidth) {
        scale = static_cast<float>(effectiveMaxWidth) / static_cast<float>(visualWidth);
    }
    Field<float>(item, kTextScaleOffset) = scale;
    return true;
}

void OpenMainMenu(void* mainMenu) {
    if (!mainMenu || !g_setMenuState) {
        return;
    }

    void* table = Field<void*>(mainMenu, kMenuTableOffset);
    if (!table) {
        return;
    }
    void* state = static_cast<std::uint8_t*>(table) + kMainMenuId * kMenuStateSize;
    g_setMenuState(mainMenu, kMainMenuId, state);
}

void __cdecl OnYes(void* mainMenu) {
    if (g_apiPromptActive) {
        std::function<void()> callback = std::move(g_apiOnConfirm);
        g_apiOnDecline = {};
        g_apiPromptActive = false;
        if (callback) callback();
        if (!g_apiPromptActive) OpenMainMenu(mainMenu);
        return;
    }
    Core::Logger::Info("UpdatePromptMenu: installing available HookCrashers update");
    if (!HookCrashers::Updater::AutoUpdater::InstallAvailableUpdate() &&
        !TryOpenPendingApiPrompt(mainMenu)) {
        OpenMainMenu(mainMenu);
    }
}

void __cdecl OnNo(void* mainMenu) {
    if (g_apiPromptActive) {
        std::function<void()> callback = std::move(g_apiOnDecline);
        g_apiOnConfirm = {};
        g_apiPromptActive = false;
        if (callback) callback();
        if (!g_apiPromptActive) OpenMainMenu(mainMenu);
        return;
    }
    Core::Logger::Info("UpdatePromptMenu: No selected; returning to the main menu");
    if (!TryOpenPendingApiPrompt(mainMenu)) {
        OpenMainMenu(mainMenu);
    }
}

bool ConfigureAndOpenPrompt(void* mainMenu, const wchar_t* title, const wchar_t* message) {
    void* table = Field<void*>(mainMenu, kMenuTableOffset);
    if (!table) {
        return false;
    }

    void* state = static_cast<std::uint8_t*>(table) + kConfirmationMenuId * kMenuStateSize;
    if (Field<std::uint16_t>(state, kMenuItemCountOffset) < 7) {
        Core::Logger::Error("UpdatePromptMenu: confirmation menu has fewer than seven text items");
        return false;
    }

    auto** items = reinterpret_cast<void**>(static_cast<std::uint8_t*>(state) + kMenuItemsOffset);
    if (!items[0] || !items[1] || !items[2] || !items[3] || !items[4] || !items[5] || !items[6]) {
        Core::Logger::Error("UpdatePromptMenu: confirmation text items are incomplete");
        return false;
    }

    // The stock handler lays item 2 above item 3. Rebind those slots so Yes is above No.
    Field<MenuCallbackFn>(items[2], kTextCallbackOffset) = OnYes;
    Field<MenuCallbackFn>(items[3], kTextCallbackOffset) = OnNo;
    Field<void*>(state, kBackCallbackOffset) = nullptr;
    Field<std::uint16_t>(state, kConfiguredTargetOffset) = kMainMenuId;
    Field<std::uint16_t>(state, kSelectedIndexOffset) = 2;

    const bool textReady =
        SetRawText(items[0], title, 64) &&
        SetRawText(items[1], message, 72) &&
        SetRawText(items[2], L"Yes", 16) &&
        SetRawText(items[3], L"No", 16) &&
        SetRawText(items[4], L"") &&
        SetRawText(items[5], L"") &&
        SetRawText(items[6], L"");
    if (!textReady) {
        Core::Logger::Error("UpdatePromptMenu: failed to allocate raw menu text");
        return false;
    }

    g_setMenuState(mainMenu, kConfirmationMenuId, state);
    Field<std::uint16_t>(state, kSelectedIndexOffset) = 2;
    Core::Logger::Info("UpdatePromptMenu: opened native bookbg confirmation menu");
    return true;
}

bool TryOpenPendingApiPrompt(void* mainMenu) {
    if (!g_apiPromptPending || g_apiPromptActive ||
        !ConfigureAndOpenPrompt(mainMenu, L"", g_apiPromptMessage.c_str())) {
        return false;
    }
    g_apiPromptPending = false;
    g_apiPromptActive = true;
    return true;
}

void* __fastcall HookedMainMenuBuilder(void* self, void*, void* scene, int flags) {
    constexpr DWORD kMaximumUpdateWaitMs = 15000;
    DWORD waitedMs = 0;
    while (!g_updateCheckComplete.load(std::memory_order_acquire) &&
           waitedMs < kMaximumUpdateWaitMs) {
        Sleep(10);
        waitedMs += 10;
    }
    if (!g_updateCheckComplete.load(std::memory_order_acquire)) {
        Core::Logger::Warn("UpdatePromptMenu: update check timed out; opening main menu");
    }

    void* result = g_originalMainMenuBuilder(self, scene, flags);
    g_mainMenu = self;

    if (!g_promptShown) {
        g_promptShown = true;
        if (g_startupUpdateAvailable.load(std::memory_order_relaxed)) {
            ConfigureAndOpenPrompt(
                self,
                L"HookCrashers has been updated",
                L"Would you like to update HookCrashers? (Game will restart)");
        } else {
            TryOpenPendingApiPrompt(self);
        }
    } else {
        TryOpenPendingApiPrompt(self);
    }
    return result;
}

} // namespace

void SetStartupUpdateAvailable(bool available) {
    g_startupUpdateAvailable.store(available, std::memory_order_relaxed);
    g_updateCheckComplete.store(true, std::memory_order_release);
}

bool Install() {
    if (g_installed) {
        return true;
    }

    g_originalMainMenuBuilder = reinterpret_cast<MainMenuBuilderFn>(
        Core::Memory::Va(kMainMenuBuilderRva));
    g_setMenuState = reinterpret_cast<SetMenuStateFn>(Core::Memory::Va(kSetMenuStateRva));
    g_getAllocator = reinterpret_cast<GetAllocatorFn>(Core::Memory::Va(kGetAllocatorRva));
    g_allocate = reinterpret_cast<AllocateFn>(Core::Memory::Va(kAllocateRva));
    g_free = reinterpret_cast<FreeFn>(Core::Memory::Va(kFreeRva));

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)g_originalMainMenuBuilder, HookedMainMenuBuilder);
    const LONG result = DetourTransactionCommit();
    if (result != NO_ERROR) {
        Core::Logger::Error("UpdatePromptMenu: failed to attach main-menu hook, detours=%ld", result);
        return false;
    }

    g_installed = true;
    Core::Logger::Info("UpdatePromptMenu: main-menu builder hook installed");
    return true;
}

bool CreatePrompt(const std::string& message,
                  std::function<void()> onConfirm,
                  std::function<void()> onDecline) {
    if (message.empty() || g_apiPromptPending || g_apiPromptActive) return false;

    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, message.c_str(), -1, nullptr, 0);
    if (length <= 1) {
        Core::Logger::Error("UpdatePromptMenu: CreatePrompt received invalid UTF-8");
        return false;
    }

    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            message.c_str(), -1, wide.data(), length) == 0) {
        return false;
    }
    wide.resize(static_cast<std::size_t>(length - 1));
    g_apiPromptMessage = std::move(wide);
    g_apiOnConfirm = std::move(onConfirm);
    g_apiOnDecline = std::move(onDecline);
    g_apiPromptPending = true;

    if (g_mainMenu && g_promptShown &&
        ConfigureAndOpenPrompt(g_mainMenu, L"", g_apiPromptMessage.c_str())) {
        g_apiPromptPending = false;
        g_apiPromptActive = true;
    }
    return true;
}

void Remove() {
    if (g_installed && g_originalMainMenuBuilder) {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach(&(PVOID&)g_originalMainMenuBuilder, HookedMainMenuBuilder);
        const LONG result = DetourTransactionCommit();
        if (result != NO_ERROR) {
            Core::Logger::Warn("UpdatePromptMenu: failed to detach main-menu hook, detours=%ld", result);
        }
    }

    g_installed = false;
    g_promptShown = false;
    g_startupUpdateAvailable.store(false, std::memory_order_relaxed);
    g_updateCheckComplete.store(false, std::memory_order_relaxed);
    g_mainMenu = nullptr;
    g_apiPromptPending = false;
    g_apiPromptActive = false;
    g_apiPromptMessage.clear();
    g_apiOnConfirm = {};
    g_apiOnDecline = {};
}

} // namespace HookCrashers::UI::UpdatePromptMenu
