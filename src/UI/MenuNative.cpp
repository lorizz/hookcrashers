#include "MenuNative.h"

#include "MenuModel.h"
#include "MenuTableExpansion.h"
#include "../Core/Logger.h"
#include "../Core/Memory.h"
#include "../Game/MenuTable.h"
#include "../SWF/MenuGraphicAssets.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <vector>

// All RVAs below belong to castle.exe build 0x6A4D42BF (2026-07-07) and were
// verified by disassembly; see docs/menu-api.md "Native reference".
namespace HookCrashers::UI::Menus::Native {
namespace {

namespace Table = Game::MenuTable;

// --- Native functions -------------------------------------------------------
constexpr std::uintptr_t kSetMenuStateRva = 0x12C4C0;        // thiscall(main, i16 id, state), ret 8
constexpr std::uintptr_t kFinalizeAndAddRva = 0x1523E0;      // thiscall(state, item) + xmm2/xmm3 x,y, ret 4
constexpr std::uintptr_t kTextItemCtorRva = 0x153EE0;        // thiscall(item, owner, cb, locId, maxWidth, selectable), ret 0x14
constexpr std::uintptr_t kSelectorCtorRva = 0x154900;        // thiscall(item, owner, onChange, labelLocId, selectable), ret 0x10
constexpr std::uintptr_t kTitleCtorRva = 0x154780;           // thiscall(item, locId, unused), ret 8
constexpr std::uintptr_t kSelectorAddRawOptionRva = 0x155280;// thiscall(item, const char*), ret 4
constexpr std::uintptr_t kGraphicMenuItemRva = 0x1556C0;     // thiscall(item, scene, strObj*, depth, x, y), ret 0x14
constexpr std::uintptr_t kInitGraphicMenuItemRva = 0x1555F0; // thiscall(item, scene), ret 4
constexpr std::uintptr_t kButtonSetPresetRva = 0x156310;     // thiscall(button, ctx)
constexpr std::uintptr_t kPrepareStringObjectRva = 0x02EE80; // thiscall(strObj, const char*), ret 4
constexpr std::uintptr_t kGetAllocatorRva = 0x0F6AC0;
constexpr std::uintptr_t kAllocateRva = 0x0DFE70;            // thiscall(allocator, size)
constexpr std::uintptr_t kFreeRva = 0x0F6DF0;                // cdecl(ptr)
constexpr std::uintptr_t kAllocatorReadyByteRva = 0x360EE1;
constexpr std::uintptr_t kAllocatorCriticalSectionRva = 0x361118;

// --- Native data -------------------------------------------------------------
constexpr std::uintptr_t kMainMenuVtableRva = 0x1D07B8;
constexpr std::uintptr_t kHandlerVtableRva = 0x1D0AD0;
constexpr std::uintptr_t kTextItemVtableRva = 0x1D0B08;
constexpr std::uintptr_t kSelectorVtableRva = 0x1D0B38;
constexpr std::uintptr_t kTitleVtableRva = 0x1D0B68;
constexpr std::uintptr_t kButtonVtableRva = 0x1D0B98;

// --- Layout --------------------------------------------------------------------
constexpr std::size_t kMainCurrentHandler = 0x38;
constexpr std::size_t kMainActiveId = 0x3C;
constexpr std::size_t kMainTable = 0x40;
constexpr std::size_t kMainScene = 0x04;

constexpr std::size_t kStateMenuItemCount = 0x04;
constexpr std::size_t kStateGraphicCount = 0x06;
constexpr std::size_t kStateButtonCount = 0x08;
constexpr std::size_t kStateHotspotCount = 0x0A;
constexpr std::size_t kStateSelected = 0x0C;
constexpr std::size_t kStateTransitionFlags = 0x12;
constexpr std::size_t kStateKeepSelection = 0x14;
constexpr std::size_t kStateHotspots = 0x1C;
constexpr std::size_t kStateMenuItems = 0x44;
constexpr std::size_t kStateGraphics = 0x144;
constexpr std::size_t kStateButtons = 0x184;
constexpr std::size_t kStateConfirmOverride = 0x1B0;
constexpr std::size_t kStateOwner = 0x1AC;
constexpr std::size_t kStateBack = 0x1B4;
constexpr std::size_t kStateAltActions = 0x1B8;       // 0x1B8..0x1C4, four callbacks
constexpr std::size_t kStateUpdate = 0x1C8;
constexpr std::size_t kStateExtraAction = 0x1CC;
constexpr std::size_t kStateInit = 0x1D0;
constexpr std::size_t kStatePreUpdate = 0x1D4;
constexpr std::size_t kStateReturnTarget = 0x1DA;
constexpr std::size_t kStateInputEnabled = 0x15;     // set to 1 by the fade-in (0x1539E0) when it completes
constexpr std::size_t kStatePendingAction = 0x18;
constexpr std::size_t kStateInputPlayer = 0x10;      // i16, see UpdateTrampoline
constexpr std::size_t kStateInputBlocked = 0x1F0;    // u8, input ignored while non-zero (0x152D10)
constexpr std::size_t kStateFlashTime = 0x1F8;       // confirm flash timer (0x153910)
constexpr std::size_t kStateFadeDirection = 0x201;   // i8: 1 fade in, -1 fade out, 0 idle
constexpr std::size_t kStateFadeAlpha = 0x204;       // float 0..1, copied to every item's alpha
constexpr std::size_t kItemColor = 0x10;             // u32 0xRRGGBBAA, selected item is animated by the menu
constexpr std::size_t kItemFallbackColor = 0x14;
constexpr std::uint32_t kDisabledColor = 0x8A8A8AFFu;

constexpr std::size_t kItemDisabled = 0x2D;
constexpr std::size_t kItemText = 0x3C;
constexpr std::size_t kItemFitScale = 0x44;
constexpr std::size_t kItemTextLength = 0x4A;
constexpr std::size_t kItemVisualWidth = 0x4C;
constexpr std::size_t kItemMaxWidth = 0x4E;
constexpr std::size_t kSelectorOptions = 0x50;        // vector<Option*> begin
constexpr std::size_t kSelectorOptionCount = 0x60;    // u16
constexpr std::size_t kSelectorIndex = 0x62;          // u16
constexpr std::size_t kSelectorWrap = 0x66;           // u8

constexpr int kTextItemSize = 0x50;
constexpr int kSelectorSize = 0x68;
constexpr int kGraphicItemSize = 0x5C;
constexpr int kButtonItemSize = 0x60;
constexpr int kBookBackgroundDepth = 2003;            // shared by the native book menus
constexpr int kGraphicDepthBase = 11000;              // + slot * 16 + n, clear of native/online depths
constexpr std::uint16_t kTitleMaxWidth = 64;
constexpr std::uint16_t kItemMaxWidthUnits = 44;  // longer text is scaled down to fit

using SetMenuStateFn = void(__thiscall*)(void* mainMenu, std::int16_t id, void* state);
using TextItemCtorFn = void*(__thiscall*)(void* item, void* owner, void* callback, std::uint32_t locId,
                                          std::uint32_t maxWidth, std::uint32_t selectable);
using SelectorCtorFn = void*(__thiscall*)(void* item, void* owner, void* onChange, std::uint32_t labelLocId,
                                          std::uint32_t selectable);
using TitleCtorFn = void*(__thiscall*)(void* item, std::uint32_t locId, std::uint32_t unused);
using AddRawOptionFn = void(__thiscall*)(void* item, const char* text);
using GraphicMenuItemFn = void*(__thiscall*)(void* item, void* scene, void* exportName, int depth, float x, float y);
using InitGraphicMenuItemFn = void*(__thiscall*)(void* item, void* scene);
using ButtonSetPresetFn = void(__thiscall*)(void* button, void* context);
using PrepareStringObjectFn = void*(__thiscall*)(void* stringObject, const char* value);
using GetAllocatorFn = void*(__cdecl*)();
using AllocateFn = void*(__thiscall*)(void* allocator, int size);
using FreeFn = void(__cdecl*)(void* allocation);

struct Signature {
    std::uintptr_t rva;
    std::array<std::uint8_t, 16> bytes;
    int relocAt;         // offset of one relocated absolute dword inside `bytes`, or -1
    bool detouredByHc = false; // other HookCrashers modules may have replaced the prologue with a Detours jmp
};

// First 16 bytes of each function at preferred base 0x400000.
constexpr Signature kSignatures[] = {
    {kTextItemCtorRva, {0x55,0x8B,0xEC,0x6A,0xFF,0x68,0x28,0x31,0x5A,0x00,0x64,0xA1,0,0,0,0}, 6},
    {kSelectorCtorRva, {0x55,0x8B,0xEC,0x6A,0xFF,0x68,0x9B,0x31,0x5A,0x00,0x64,0xA1,0,0,0,0}, 6},
    {kTitleCtorRva, {0x55,0x8B,0xEC,0x6A,0xFF,0x68,0x60,0x31,0x5A,0x00,0x64,0xA1,0,0,0,0}, 6},
    {kSelectorAddRawOptionRva, {0x55,0x8B,0xEC,0x6A,0xFF,0x68,0x3F,0x32,0x5A,0x00,0x64,0xA1,0,0,0,0}, 6},
    {kFinalizeAndAddRva, {0x55,0x8B,0xEC,0x83,0xEC,0x08,0x0F,0x2E,0x1D,0xA0,0x39,0x63,0x00,0x56,0x57,0x8B}, 9},
    // Detoured by MenuOnlineCompatibilityHook before the Menu API installs.
    {kSetMenuStateRva, {0x55,0x8B,0xEC,0x53,0x56,0x8B,0xF1,0x57,0xBF,0xFE,0xFF,0xFF,0xFF,0x8B,0x4E,0x38}, -1, true},
    {kGraphicMenuItemRva, {0x55,0x8B,0xEC,0x6A,0xFF,0x68,0x60,0x31,0x5A,0x00,0x64,0xA1,0,0,0,0}, 6},
    {kInitGraphicMenuItemRva, {0x55,0x8B,0xEC,0x6A,0xFF,0x68,0x70,0x32,0x5A,0x00,0x64,0xA1,0,0,0,0}, 6},
    {kButtonSetPresetRva, {0x55,0x8B,0xEC,0x6A,0xFF,0x68,0x10,0x34,0x5A,0x00,0x64,0xA1,0,0,0,0}, 6},
    {kPrepareStringObjectRva, {0x55,0x8B,0xEC,0x6A,0xFF,0x68,0x38,0xBD,0x58,0x00,0x64,0xA1,0,0,0,0}, 6},
    // Detoured by GamePoolExpansion (installed after the Menu API today, but order may change).
    {kAllocateRva, {0x55,0x8B,0xEC,0x83,0xEC,0x0C,0x53,0x56,0x8B,0xF1,0xBB,0x01,0x00,0x00,0x00,0x33}, -1, true},
    {kFreeRva, {0x55,0x8B,0xEC,0x6A,0xFF,0x68,0x60,0xBB,0x58,0x00,0x64,0xA1,0,0,0,0}, 6},
};

struct VtableCheck {
    std::uintptr_t vtableRva;
    std::uintptr_t firstEntryRva;
};

constexpr VtableCheck kVtables[] = {
    {kHandlerVtableRva, 0x0A8EF0},
    {kTextItemVtableRva, 0x153E60},
    {kSelectorVtableRva, 0x1549F0},
    {kTitleVtableRva, 0x154870},
    {kButtonVtableRva, 0x1562B0},
};

// --- Runtime state (game thread) ----------------------------------------------
struct SlotState {
    std::string menuId;
    void* mainMenu = nullptr;
    std::uint32_t revision = 0;
    bool built = false;
    std::vector<int> nativeToItem;    // menuItems index -> model item index (-1: title/label-less)
    std::vector<void*> itemObjects;   // model item index -> native object
    std::vector<std::uint32_t> defaultColors; // model item index -> native color at build time
};

std::atomic<bool> g_available {false};
std::atomic<DWORD> g_gameThread {0};
void* g_mainMenu = nullptr;
void* g_table = nullptr;
std::array<SlotState, Table::kModMenuSlotCount> g_slots;

std::mutex g_requestLock;            // requests from any thread
std::string g_pendingOpen;
std::set<std::string> g_dirty;

template <typename T>
T& Field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

template <typename Fn>
Fn NativeFn(std::uintptr_t rva) {
    return reinterpret_cast<Fn>(Core::Memory::Va(rva));
}

bool VerifySignatures() {
    const auto base = Core::Memory::GameBase();
    for (const Signature& signature : kSignatures) {
        auto expected = signature.bytes;
        if (signature.relocAt >= 0) {
            std::uint32_t value = 0;
            std::memcpy(&value, expected.data() + signature.relocAt, 4);
            value = static_cast<std::uint32_t>(value - 0x400000 + base);
            std::memcpy(expected.data() + signature.relocAt, &value, 4);
        }
        const auto* actual = reinterpret_cast<const std::uint8_t*>(Core::Memory::Va(signature.rva));
        // A Detours hook overwrites the first 5+ bytes with `jmp rel32`; the
        // executable itself is already pinned by the TimeDateStamp check.
        if (signature.detouredByHc && actual[0] == 0xE9) continue;
        // MSVC SEH prologues end with `mov eax, fs:[0]`; its zero displacement is part of the bytes.
        if (std::memcmp(actual, expected.data(), expected.size()) != 0) {
            Core::Logger::Error("Menu API: signature mismatch at rva=0x%08X", static_cast<unsigned>(signature.rva));
            return false;
        }
    }
    for (const VtableCheck& check : kVtables) {
        const auto entry = *reinterpret_cast<const std::uintptr_t*>(Core::Memory::Va(check.vtableRva));
        if (entry != Core::Memory::Va(check.firstEntryRva)) {
            Core::Logger::Error("Menu API: vtable mismatch at rva=0x%08X", static_cast<unsigned>(check.vtableRva));
            return false;
        }
    }
    return true;
}

// --- Game allocator & text -------------------------------------------------------
void* AllocateGame(std::size_t size) {
    if (*reinterpret_cast<const std::uint8_t*>(Core::Memory::Va(kAllocatorReadyByteRva)) == 0) return nullptr;
    auto* section = reinterpret_cast<LPCRITICAL_SECTION>(Core::Memory::Va(kAllocatorCriticalSectionRva));
    EnterCriticalSection(section);
    void* allocator = NativeFn<GetAllocatorFn>(kGetAllocatorRva)();
    void* memory = allocator ? NativeFn<AllocateFn>(kAllocateRva)(allocator, static_cast<int>(size)) : nullptr;
    LeaveCriticalSection(section);
    return memory;
}

void FreeGame(void* memory) {
    if (memory) NativeFn<FreeFn>(kFreeRva)(memory);
}

std::wstring Widen(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        std::wstring ascii;
        for (unsigned char c : text) ascii.push_back(c < 0x80 ? static_cast<wchar_t>(c) : L'?');
        return ascii;
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

wchar_t* GameString(const std::wstring& text) {
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    auto* copy = static_cast<wchar_t*>(AllocateGame(bytes));
    if (copy) std::memcpy(copy, text.c_str(), bytes);
    return copy;
}

// Same width/scale bookkeeping as the native SetLocalizedText (RVA 0x154210).
bool SetItemText(void* item, const std::string& utf8, std::uint16_t maxWidth) {
    const std::wstring text = Widen(utf8);
    if (text.size() > 0xFFFF) return false;
    wchar_t* copy = GameString(text);
    if (!copy) return false;
    FreeGame(Field<void*>(item, kItemText));
    Field<wchar_t*>(item, kItemText) = copy;

    unsigned units = 0;
    for (wchar_t ch : text) units += ch < 0x80 ? 8u : 11u;
    const auto visual = static_cast<std::uint16_t>((std::min)(units >> 3, 0xFFFFu));
    Field<std::uint16_t>(item, kItemTextLength) = static_cast<std::uint16_t>(text.size());
    Field<std::uint16_t>(item, kItemVisualWidth) = visual;
    if (maxWidth) Field<std::uint16_t>(item, kItemMaxWidth) = maxWidth;
    const std::uint16_t limit = Field<std::uint16_t>(item, kItemMaxWidth);
    Field<float>(item, kItemFitScale) = (limit && visual > limit) ? static_cast<float>(limit) / visual : 1.0f;
    return true;
}

bool SetSelectorOption(void* selector, std::size_t index, const std::string& utf8) {
    if (index >= Field<std::uint16_t>(selector, kSelectorOptionCount)) return false;
    auto** options = Field<void**>(selector, kSelectorOptions);
    if (!options || !options[index]) return false;
    wchar_t* copy = GameString(Widen(utf8));
    if (!copy) return false;
    auto*& text = *static_cast<wchar_t**>(options[index]);
    FreeGame(text);
    text = copy;
    return true;
}

// FinalizeAndAddElement takes the explicit position in xmm2/xmm3; 0/0 keeps the
// item's own position or lets the menu lay it out (autoLayout flag at +0x2C).
void __fastcall FinalizeItem(void* state, void*, void* item) {
    const auto fn = Core::Memory::Va(kFinalizeAndAddRva);
    __asm {
        mov ecx, state
        push item
        xorps xmm2, xmm2
        xorps xmm3, xmm3
        call fn
    }
}

void* CreateGraphic(void* mainMenu, void* state, const char* exportName, int depth, float x, float y) {
    void* storage = AllocateGame(kGraphicItemSize);
    if (!storage) return nullptr;
    std::uint32_t name[22] {};   // native string object, 0x58 bytes
    NativeFn<PrepareStringObjectFn>(kPrepareStringObjectRva)(name, exportName);
    void* item = NativeFn<GraphicMenuItemFn>(kGraphicMenuItemRva)(
        storage, Field<void*>(mainMenu, kMainScene), name, depth, x, y);
    if (static_cast<int>(name[3]) > 0) FreeGame(reinterpret_cast<void*>(name[0])); // heap-backed name buffer
    if (item) FinalizeItem(state, nullptr, item);
    return item;
}

void* CreateBackButton(void* mainMenu, void* state) {
    void* storage = AllocateGame(kButtonItemSize);
    if (!storage) return nullptr;
    NativeFn<InitGraphicMenuItemFn>(kInitGraphicMenuItemRva)(storage, Field<void*>(mainMenu, kMainScene));
    Field<std::uintptr_t>(storage, 0) = Core::Memory::Va(kButtonVtableRva);
    Field<std::uint32_t>(storage, 0x5C) = 0; // preset 0: Back
    NativeFn<ButtonSetPresetFn>(kButtonSetPresetRva)(storage, nullptr);
    const std::uint32_t fallback = Field<std::uint32_t>(storage, 0x14);
    Field<std::uint32_t>(storage, 0x10) = fallback ? fallback : 0xAAAAAAFF;
    Field<std::uintptr_t>(storage, 0) = Core::Memory::Va(kButtonVtableRva);
    FinalizeItem(state, nullptr, storage);
    return storage;
}

// --- Lookups -----------------------------------------------------------------------
std::uint16_t SlotMenuId(std::size_t slot) {
    return static_cast<std::uint16_t>(Table::kFirstModMenuId + slot);
}

void* HandlerFor(std::size_t slot) {
    return static_cast<std::uint8_t*>(g_table) + SlotMenuId(slot) * Table::kHandlerSize;
}

bool MainMenuAlive(void* mainMenu) {
    if (!mainMenu || !g_table) return false;
    __try {
        return Field<std::uintptr_t>(mainMenu, 0) == Core::Memory::Va(kMainMenuVtableRva) &&
               Field<void*>(mainMenu, kMainTable) == g_table &&
               *reinterpret_cast<const std::uint32_t*>(static_cast<std::uint8_t*>(g_table) - 4) == Table::kTotalCount;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int SlotOfHandler(void* handler) {
    if (!g_table || !handler) return -1;
    const auto first = reinterpret_cast<std::uintptr_t>(HandlerFor(0));
    const auto value = reinterpret_cast<std::uintptr_t>(handler);
    if (value < first) return -1;
    const auto delta = value - first;
    if (delta % Table::kHandlerSize) return -1;
    const auto slot = delta / Table::kHandlerSize;
    return slot < Table::kModMenuSlotCount ? static_cast<int>(slot) : -1;
}

int SlotFor(const std::string& menuId, bool assign) {
    for (std::size_t i = 0; i < g_slots.size(); ++i) {
        if (g_slots[i].menuId == menuId) return static_cast<int>(i);
    }
    if (!assign) return -1;
    for (std::size_t i = 0; i < g_slots.size(); ++i) {
        if (g_slots[i].menuId.empty()) {
            g_slots[i] = {};
            g_slots[i].menuId = menuId;
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool OnGameThread() {
    return GetCurrentThreadId() == g_gameThread.load();
}

bool OpenNow(const std::string& menuId);

// --- Callbacks called by the game (cdecl, argument = item/handler owner) ---------
void InvokeSafely(const Item& item, int value) {
    if (!item.onActivate) return;
    try {
        item.onActivate(value);
    } catch (...) {
        Core::Logger::Error("Menu API: callback for item '%s' threw an exception", item.id.c_str());
    }
}

bool ResolveSelected(void* owner, int& slot, int& itemIndex, void*& handler) {
    if (owner != g_mainMenu || !g_mainMenu) return false;
    handler = Field<void*>(owner, kMainCurrentHandler);
    slot = SlotOfHandler(handler);
    if (slot < 0 || !g_slots[slot].built) return false;
    const std::uint16_t selected = Field<std::uint16_t>(handler, kStateSelected);
    const auto& map = g_slots[slot].nativeToItem;
    itemIndex = selected < map.size() ? map[selected] : -1;
    return itemIndex >= 0;
}

void __cdecl ActivateTrampoline(void* owner) {
    int slot = -1, itemIndex = -1;
    void* handler = nullptr;
    if (!ResolveSelected(owner, slot, itemIndex, handler)) return;
    Item item;
    if (!Registry::Instance().ItemAt(g_slots[slot].menuId, static_cast<std::size_t>(itemIndex), item) || !item.enabled) return;
    if (item.kind == ItemKind::Submenu) {
        if (!OpenNow(item.target)) {
            Core::Logger::Error("Menu API: submenu '%s' of '%s' could not be opened", item.target.c_str(), item.id.c_str());
        }
        return;
    }
    InvokeSafely(item, 0);
}

void __cdecl ChangeTrampoline(void* owner) {
    int slot = -1, itemIndex = -1;
    void* handler = nullptr;
    if (!ResolveSelected(owner, slot, itemIndex, handler)) return;
    void* object = g_slots[slot].itemObjects[static_cast<std::size_t>(itemIndex)];
    if (!object) return;
    const int value = Field<std::uint16_t>(object, kSelectorIndex);
    Item item;
    if (Registry::Instance().CommitValue(g_slots[slot].menuId, static_cast<std::size_t>(itemIndex), value, item) == Result::Ok) {
        InvokeSafely(item, item.value);
    }
}

void ReturnFrom(void* handler) {
    const std::uint16_t target = Field<std::uint16_t>(handler, kStateReturnTarget);
    const std::uint16_t safeTarget = target < Table::kTotalCount ? target : Table::kMainMenuId;
    void* targetState = static_cast<std::uint8_t*>(g_table) + safeTarget * Table::kHandlerSize;
    NativeFn<SetMenuStateFn>(kSetMenuStateRva)(g_mainMenu, static_cast<std::int16_t>(safeTarget), targetState);
}

void __cdecl BackTrampoline(void* owner) {
    if (owner != g_mainMenu || !g_mainMenu) return;
    void* handler = Field<void*>(owner, kMainCurrentHandler);
    const int slot = SlotOfHandler(handler);
    if (slot < 0) return;
    Menu menu;
    if (Registry::Instance().Snapshot(g_slots[slot].menuId, menu) && menu.onClose) {
        try {
            menu.onClose();
        } catch (...) {
            Core::Logger::Error("Menu API: onClose of '%s' threw an exception", menu.id.c_str());
        }
    }
    // A close callback may already have opened another menu.
    if (Field<void*>(owner, kMainCurrentHandler) == handler) ReturnFrom(handler);
}

void ApplyLiveState(std::size_t slot, const Menu& menu);
void ProcessRequests();

void StartFadeIn(void* handler) {
    Field<std::uint8_t>(handler, kStateInputEnabled) = 0;
    Field<std::uint32_t>(handler, kStatePendingAction) = 0;
    Field<std::uint8_t>(handler, kStateInputBlocked) = 0;
    Field<float>(handler, kStateFlashTime) = 0.0f;
    Field<std::int8_t>(handler, kStateFadeDirection) = 1;
    Field<float>(handler, kStateFadeAlpha) = 0.0f;
}

void __cdecl UpdateTrampoline(void* owner) {
    if (owner != g_mainMenu) return;
    {
        // Input is read for the player at +0x10 (0x1518C0): -2 any controller,
        // -3 any signed-in player, 0..3 one player. SetMenuState copies it from
        // the previous menu, which before "press start" may hold none of these,
        // and then every key is ignored.
        void* current = Field<void*>(owner, kMainCurrentHandler);
        if (SlotOfHandler(current) >= 0) {
            const std::int16_t player = Field<std::int16_t>(current, kStateInputPlayer);
            if (player != -2 && player != -3 && (player < 0 || player > 3)) {
                Field<std::int16_t>(current, kStateInputPlayer) = -2;
            }
        }
    }
    // Menus re-entered through a plain SetMenuState (Back from a submenu, a
    // prompt returning here) get no fade-in and would ignore input forever.
    void* handler = Field<void*>(owner, kMainCurrentHandler);
    if (SlotOfHandler(handler) >= 0 && Field<std::uint8_t>(handler, kStateInputEnabled) == 0 &&
        Field<std::int8_t>(handler, kStateFadeDirection) == 0 && Field<float>(handler, kStateFlashTime) == 0.0f) {
        StartFadeIn(handler);
    }
    ProcessRequests();
}

// --- Building ---------------------------------------------------------------------
void ResetHandler(void* handler, bool backButton) {
    void* confirmation = static_cast<std::uint8_t*>(g_table) + Table::kConfirmationMenuId * Table::kHandlerSize;
    // Handlers in the expanded slots were built by the native MenuState
    // constructor; reuse the stock book-menu transition flags.
    Field<std::uint16_t>(handler, kStateTransitionFlags) = Field<std::uint16_t>(confirmation, kStateTransitionFlags);
    Field<std::uint8_t>(handler, kStateKeepSelection) = 1;
    Field<std::uint16_t>(handler, kStateMenuItemCount) = 0;
    Field<std::uint16_t>(handler, kStateGraphicCount) = 0;
    Field<std::uint16_t>(handler, kStateButtonCount) = 0;
    Field<std::uint16_t>(handler, kStateHotspotCount) = 0;
    Field<std::uint16_t>(handler, kStateSelected) = 0;
    std::memset(&Field<std::uint8_t>(handler, kStateHotspots), 0, 10 * sizeof(void*));
    std::memset(&Field<std::uint8_t>(handler, kStateMenuItems), 0, 64 * sizeof(void*));
    std::memset(&Field<std::uint8_t>(handler, kStateGraphics), 0, 16 * sizeof(void*));
    std::memset(&Field<std::uint8_t>(handler, kStateButtons), 0, 8 * sizeof(void*));
    Field<void*>(handler, kStateOwner) = g_mainMenu;
    Field<void*>(handler, kStateConfirmOverride) = nullptr;
    Field<void*>(handler, kStateBack) = backButton ? reinterpret_cast<void*>(&BackTrampoline) : nullptr;
    for (std::size_t offset = kStateAltActions; offset < kStateUpdate; offset += 4) Field<void*>(handler, offset) = nullptr;
    Field<void*>(handler, kStateUpdate) = reinterpret_cast<void*>(&UpdateTrampoline);
    Field<void*>(handler, kStateExtraAction) = nullptr;
    Field<void*>(handler, kStateInit) = nullptr;
    Field<void*>(handler, kStatePreUpdate) = nullptr;
}

bool Build(std::size_t slot, const Menu& menu) {
    SlotState& state = g_slots[slot];
    void* handler = HandlerFor(slot);
    if (Field<std::uintptr_t>(handler, 0) != Core::Memory::Va(kHandlerVtableRva)) {
        Core::Logger::Error("Menu API: slot %u is not a native MenuState", static_cast<unsigned>(SlotMenuId(slot)));
        return false;
    }
    ResetHandler(handler, menu.backButton);
    state.nativeToItem.clear();
    state.itemObjects.assign(menu.items.size(), nullptr);
    state.defaultColors.assign(menu.items.size(), 0);

    auto recordText = [&](int itemIndex) {
        const std::uint16_t count = Field<std::uint16_t>(handler, kStateMenuItemCount);
        state.nativeToItem.resize(count, -1);
        if (count) state.nativeToItem[count - 1] = itemIndex;
    };

    if (menu.background) {
        CreateGraphic(g_mainMenu, handler, "bookbg", kBookBackgroundDepth, 0.0f, 0.0f);
    }
    if (!menu.title.empty()) {
        void* title = AllocateGame(kTextItemSize);
        if (!title) return false;
        NativeFn<TitleCtorFn>(kTitleCtorRva)(title, 0, 0);
        SetItemText(title, menu.title, kTitleMaxWidth);
        FinalizeItem(handler, nullptr, title);
        recordText(-1);
    }

    int graphicIndex = 0;
    for (std::size_t i = 0; i < menu.items.size(); ++i) {
        const Item& item = menu.items[i];
        void* object = nullptr;
        switch (item.kind) {
        case ItemKind::Label:
        case ItemKind::Button:
        case ItemKind::Submenu: {
            object = AllocateGame(kTextItemSize);
            if (!object) return false;
            const bool selectable = item.kind != ItemKind::Label;
            NativeFn<TextItemCtorFn>(kTextItemCtorRva)(object, g_mainMenu,
                selectable ? reinterpret_cast<void*>(&ActivateTrampoline) : nullptr, 0, 0, selectable ? 1 : 0);
            SetItemText(object, item.label, kItemMaxWidthUnits);
            Field<std::uint8_t>(object, kItemDisabled) = item.enabled ? 0 : 1;
            FinalizeItem(handler, nullptr, object);
            recordText(static_cast<int>(i));
            break;
        }
        case ItemKind::Toggle:
        case ItemKind::Choice: {
            object = AllocateGame(kSelectorSize);
            if (!object) return false;
            NativeFn<SelectorCtorFn>(kSelectorCtorRva)(object, g_mainMenu, reinterpret_cast<void*>(&ChangeTrampoline), 0, 1);
            SetItemText(object, item.label, kItemMaxWidthUnits);
            for (std::size_t o = 0; o < item.options.size(); ++o) {
                NativeFn<AddRawOptionFn>(kSelectorAddRawOptionRva)(object, "-");
                SetSelectorOption(object, o, item.options[o]);
            }
            Field<std::uint16_t>(object, kSelectorIndex) = static_cast<std::uint16_t>(item.value);
            Field<std::uint8_t>(object, kSelectorWrap) = 1;
            Field<std::uint8_t>(object, kItemDisabled) = item.enabled ? 0 : 1;
            FinalizeItem(handler, nullptr, object);
            recordText(static_cast<int>(i));
            break;
        }
        case ItemKind::Graphic: {
            if (!SWF::MenuGraphicAssets::IsAvailableInScene(item.graphic)) {
                Core::Logger::Warn("Menu API: graphic '%s' of menu '%s' is not loaded in MENU; skipped",
                                   item.graphic.c_str(), menu.id.c_str());
                break;
            }
            const std::string alias = SWF::MenuGraphicAssets::AliasFor(item.graphic);
            object = CreateGraphic(g_mainMenu, handler, alias.c_str(),
                                   kGraphicDepthBase + static_cast<int>(slot) * 16 + graphicIndex++, item.x, item.y);
            break;
        }
        }
        state.itemObjects[i] = object;
        if (object && item.kind != ItemKind::Graphic) state.defaultColors[i] = Field<std::uint32_t>(object, kItemColor);
    }
    if (menu.backButton) CreateBackButton(g_mainMenu, handler);

    state.mainMenu = g_mainMenu;
    state.revision = menu.revision;
    state.built = true;
    Core::Logger::Info("Menu API: built '%s' in slot %u (items=%u text=%u graphics=%u)",
                       menu.id.c_str(), static_cast<unsigned>(SlotMenuId(slot)),
                       static_cast<unsigned>(menu.items.size()),
                       static_cast<unsigned>(Field<std::uint16_t>(handler, kStateMenuItemCount)),
                       static_cast<unsigned>(Field<std::uint16_t>(handler, kStateGraphicCount)));
    return true;
}

void ApplyLiveState(std::size_t slot, const Menu& menu) {
    SlotState& state = g_slots[slot];
    for (std::size_t i = 0; i < menu.items.size() && i < state.itemObjects.size(); ++i) {
        void* object = state.itemObjects[i];
        const Item& item = menu.items[i];
        if (!object || item.kind == ItemKind::Graphic) continue;
        SetItemText(object, item.label, 0);
        if (IsSelectable(item)) Field<std::uint8_t>(object, kItemDisabled) = item.enabled ? 0 : 1;
        std::uint32_t color = item.color;
        if (!color) color = (IsSelectable(item) && !item.enabled) ? kDisabledColor : state.defaultColors[i];
        if (color) {
            Field<std::uint32_t>(object, kItemColor) = color;
            Field<std::uint32_t>(object, kItemFallbackColor) = color;
        }
        if (item.kind == ItemKind::Toggle || item.kind == ItemKind::Choice) {
            if (Field<std::uint16_t>(object, kSelectorOptionCount) == item.options.size()) {
                for (std::size_t o = 0; o < item.options.size(); ++o) SetSelectorOption(object, o, item.options[o]);
            }
            Field<std::uint16_t>(object, kSelectorIndex) = static_cast<std::uint16_t>(item.value);
        }
    }
}

bool OpenNow(const std::string& menuId) {
    if (!g_available || !MainMenuAlive(g_mainMenu)) return false;
    Menu menu;
    if (!Registry::Instance().Snapshot(menuId, menu)) return false;
    const int slot = SlotFor(menuId, true);
    if (slot < 0) {
        Core::Logger::Error("Menu API: no free menu slot for '%s'", menuId.c_str());
        return false;
    }
    SlotState& state = g_slots[static_cast<std::size_t>(slot)];
    if (!state.built || state.mainMenu != g_mainMenu || state.revision != menu.revision) {
        if (!Build(static_cast<std::size_t>(slot), menu)) {
            state.built = false;
            return false;
        }
    }
    ApplyLiveState(static_cast<std::size_t>(slot), menu);
    {
        std::lock_guard<std::mutex> lock(g_requestLock);
        g_dirty.erase(menuId);
    }

    void* handler = HandlerFor(static_cast<std::size_t>(slot));
    const std::uint16_t active = Field<std::uint16_t>(g_mainMenu, kMainActiveId);
    const std::uint16_t self = SlotMenuId(static_cast<std::size_t>(slot));
    // Remember where Back should go, unless we are returning to a parent from one
    // of its own submenus (that would make Back bounce between the two forever).
    bool returningToParent = false;
    if (active < Table::kTotalCount) {
        const void* activeHandler = static_cast<std::uint8_t*>(g_table) + active * Table::kHandlerSize;
        returningToParent = SlotOfHandler(const_cast<void*>(activeHandler)) >= 0 &&
                            Field<std::uint16_t>(const_cast<void*>(activeHandler), kStateReturnTarget) == self;
    }
    if (active != self && !returningToParent) {
        Field<std::uint16_t>(handler, kStateReturnTarget) =
            active < Table::kTotalCount ? active : Table::kMainMenuId;
    }
    // Stock menus accept input only after their fade-in completes (0x1539E0 sets
    // +0x15); a freshly built handler has no fade running, so start one.
    StartFadeIn(handler);
    NativeFn<SetMenuStateFn>(kSetMenuStateRva)(g_mainMenu, static_cast<std::int16_t>(self), handler);
    Core::Logger::Info("Menu API: opened '%s' (return to %u)", menuId.c_str(),
                       static_cast<unsigned>(Field<std::uint16_t>(handler, kStateReturnTarget)));
    return true;
}

void ProcessRequests() {
    std::string open;
    std::set<std::string> dirty;
    {
        std::lock_guard<std::mutex> lock(g_requestLock);
        open.swap(g_pendingOpen);
        dirty.swap(g_dirty);
    }
    for (const std::string& menuId : dirty) {
        const int slot = SlotFor(menuId, false);
        if (slot < 0 || !g_slots[slot].built || g_slots[slot].mainMenu != g_mainMenu) continue;
        Menu menu;
        if (!Registry::Instance().Snapshot(menuId, menu)) continue;
        if (menu.revision != g_slots[slot].revision) continue; // rebuilt on next open
        ApplyLiveState(static_cast<std::size_t>(slot), menu);
    }
    if (!open.empty() && !OpenNow(open)) {
        Core::Logger::Error("Menu API: queued menu '%s' could not be opened", open.c_str());
    }
}

} // namespace

bool Install() {
    if (g_available) return true;
    if (!MenuTableExpansion::Install()) {
        Core::Logger::Error("Menu API unavailable: menu table could not be expanded");
        return false;
    }
    if (!VerifySignatures()) {
        Core::Logger::Error("Menu API unavailable: native menu code does not match the supported build");
        return false;
    }
    g_available = true;
    Core::Logger::Info("Menu API ready: %u mod menu slots", static_cast<unsigned>(Table::kModMenuSlotCount));
    return true;
}

bool IsAvailable() {
    return g_available;
}

void OnMainMenuBuilt(void* mainMenu) {
    if (!g_available || !mainMenu) return;
    g_gameThread = GetCurrentThreadId();
    g_mainMenu = mainMenu;
    g_table = Field<void*>(mainMenu, kMainTable);
    for (SlotState& slot : g_slots) {
        slot.built = false;
        slot.mainMenu = nullptr;
        slot.nativeToItem.clear();
        slot.itemObjects.clear();
    }
    if (!MainMenuAlive(mainMenu)) {
        Core::Logger::Error("Menu API: main menu table has %u entries, expected %u; mod menus disabled for this menu",
                            g_table ? *reinterpret_cast<const std::uint32_t*>(static_cast<std::uint8_t*>(g_table) - 4) : 0u,
                            static_cast<unsigned>(Table::kTotalCount));
        g_mainMenu = nullptr;
        return;
    }
    ProcessRequests();
}

OpenResult Open(const std::string& menuId) {
    if (!g_available) return OpenResult::Unavailable;
    if (!Registry::Instance().Exists(menuId)) return OpenResult::UnknownMenu;
    if (OnGameThread() && MainMenuAlive(g_mainMenu)) {
        return OpenNow(menuId) ? OpenResult::Opened : OpenResult::Unavailable;
    }
    std::lock_guard<std::mutex> lock(g_requestLock);
    g_pendingOpen = menuId;
    return OpenResult::Queued;
}

bool Close() {
    if (!g_available || !OnGameThread() || !MainMenuAlive(g_mainMenu)) return false;
    void* handler = Field<void*>(g_mainMenu, kMainCurrentHandler);
    if (SlotOfHandler(handler) < 0) return false;
    BackTrampoline(g_mainMenu);
    return true;
}

std::string CurrentMenuId() {
    if (!g_available || !OnGameThread() || !MainMenuAlive(g_mainMenu)) return {};
    const int slot = SlotOfHandler(Field<void*>(g_mainMenu, kMainCurrentHandler));
    return slot >= 0 ? g_slots[slot].menuId : std::string();
}

void MarkDirty(const std::string& menuId) {
    if (OnGameThread() && MainMenuAlive(g_mainMenu)) {
        const int slot = SlotFor(menuId, false);
        if (slot >= 0 && g_slots[slot].built && g_slots[slot].mainMenu == g_mainMenu) {
            Menu menu;
            if (Registry::Instance().Snapshot(menuId, menu) && menu.revision == g_slots[slot].revision) {
                ApplyLiveState(static_cast<std::size_t>(slot), menu);
            }
        }
        return;
    }
    std::lock_guard<std::mutex> lock(g_requestLock);
    g_dirty.insert(menuId);
}

} // namespace HookCrashers::UI::Menus::Native
