#include "ImGuiOverlay.h"
#include "../Config/HookCrashersConfig.h"
#include "../Core/ModLoader.h"
#include "../Game/Entities.h"
#include "../Util/Logger.h"
#include "../../include/HookCrashers/Public/Globals.h"
#include <Windows.h>
#include <d3d9.h>
#include <detours.h>
#include <imgui.h>
#include <backends/imgui_impl_dx9.h>
#include <backends/imgui_impl_win32.h>
#include <cinttypes>
#include <cstring>
#include <set>

#pragma comment(lib, "d3d9.lib")

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace HookCrashers {
namespace UI {
namespace {

using EndScene_t = HRESULT(__stdcall*)(IDirect3DDevice9*);
using Present_t = HRESULT(__stdcall*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using Reset_t = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

EndScene_t g_originalEndScene = nullptr;
Present_t g_originalPresent = nullptr;
Reset_t g_originalReset = nullptr;
HWND g_gameWindow = nullptr;
WNDPROC g_originalWndProc = nullptr;
bool g_initialized = false;
bool g_visible = false;
bool g_lastToggleDown = false;
DWORD g_lastFrameTick = 0;
bool g_inputCaptured = false;
bool g_showLobbyManagerDebug = false;
bool g_keyboardPassthrough = false;
int g_characterEntryPreviewIndex = 0;

enum class OverlayTab {
    HookCrashers = 0,
    Mods,
    Logs,
    About,
};

enum class LobbyManagerAddressMode {
    Auto = 0,
    Direct,
    Indirect,
};

struct LobbyManagerResolution {
    uintptr_t storageAddress = 0;
    const Game::LobbyManager* direct = nullptr;
    const Game::LobbyManager* indirect = nullptr;
    const Game::LobbyManager* selected = nullptr;
    bool hasReadablePointerStorage = false;
    bool directLooksValid = false;
    bool indirectLooksValid = false;
    LobbyManagerAddressMode effectiveMode = LobbyManagerAddressMode::Auto;
};

LobbyManagerAddressMode g_lobbyManagerAddressMode = LobbyManagerAddressMode::Auto;
OverlayTab g_activeTab = OverlayTab::HookCrashers;

template <typename T>
const char* BoolText(T value) {
    return value ? "true" : "false";
}

bool IsCommittedReadableProtection(DWORD protect) {
    const DWORD baseProtect = protect & 0xFF;
    if (protect & PAGE_GUARD) {
        return false;
    }
    return baseProtect == PAGE_READONLY ||
        baseProtect == PAGE_READWRITE ||
        baseProtect == PAGE_WRITECOPY ||
        baseProtect == PAGE_EXECUTE_READ ||
        baseProtect == PAGE_EXECUTE_READWRITE ||
        baseProtect == PAGE_EXECUTE_WRITECOPY;
}

bool IsReadableRange(const void* address, size_t size) {
    if (!address || size == 0) {
        return false;
    }

    uintptr_t current = reinterpret_cast<uintptr_t>(address);
    const uintptr_t end = current + size;
    while (current < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(current), &mbi, sizeof(mbi)) != sizeof(mbi)) {
            return false;
        }
        if (mbi.State != MEM_COMMIT || !IsCommittedReadableProtection(mbi.Protect)) {
            return false;
        }

        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t regionEnd = regionBase + mbi.RegionSize;
        if (regionEnd <= current) {
            return false;
        }
        current = regionEnd;
    }

    return true;
}

bool LooksLikeLobbySelectionState(const Game::LobbySelectionState* state) {
    return state && IsReadableRange(state, sizeof(Game::LobbySelectionState));
}

bool LooksLikeLobbyManager(const Game::LobbyManager* lobby) {
    if (!lobby || !IsReadableRange(lobby, sizeof(Game::LobbyManager))) {
        return false;
    }

    if (lobby->selectionState && !LooksLikeLobbySelectionState(lobby->selectionState)) {
        return false;
    }

    if (lobby->characterArrayBegin && lobby->characterArrayEnd && lobby->characterArrayEnd < lobby->characterArrayBegin) {
        return false;
    }

    if (lobby->characterArrayBegin && lobby->characterArrayCapacityEnd && lobby->characterArrayCapacityEnd < lobby->characterArrayBegin) {
        return false;
    }

    if (lobby->characterArrayEnd && lobby->characterArrayCapacityEnd && lobby->characterArrayEnd > lobby->characterArrayCapacityEnd) {
        return false;
    }

    return true;
}

bool LooksLikeCharacterEntry(const Game::CharacterEntry4240* entry) {
    return entry && IsReadableRange(entry, sizeof(Game::CharacterEntry4240));
}

const Game::CharacterRuntimeHeader40* GetCharacterRuntimeHeader(const Game::CharacterEntry4240* entry) {
    if (!entry) {
        return nullptr;
    }
    return reinterpret_cast<const Game::CharacterRuntimeHeader40*>(
        reinterpret_cast<const uint8_t*>(entry) + 0x10);
}

LobbyManagerResolution ResolveLobbyManager() {
    LobbyManagerResolution resolution{};
    if (g_moduleBase == 0) {
        return resolution;
    }

    resolution.storageAddress = g_moduleBase + Game::LOBBY_MANAGER_OFFSET;
    resolution.direct = reinterpret_cast<const Game::LobbyManager*>(resolution.storageAddress);
    resolution.directLooksValid = LooksLikeLobbyManager(resolution.direct);

    if (IsReadableRange(reinterpret_cast<const void*>(resolution.storageAddress), sizeof(void*))) {
        resolution.hasReadablePointerStorage = true;
        const auto* pointerStorage = reinterpret_cast<const Game::LobbyManager* const*>(resolution.storageAddress);
        resolution.indirect = *pointerStorage;
        resolution.indirectLooksValid = LooksLikeLobbyManager(resolution.indirect);
    }

    switch (g_lobbyManagerAddressMode) {
    case LobbyManagerAddressMode::Direct:
        resolution.selected = resolution.directLooksValid ? resolution.direct : nullptr;
        resolution.effectiveMode = LobbyManagerAddressMode::Direct;
        break;
    case LobbyManagerAddressMode::Indirect:
        resolution.selected = resolution.indirectLooksValid ? resolution.indirect : nullptr;
        resolution.effectiveMode = LobbyManagerAddressMode::Indirect;
        break;
    case LobbyManagerAddressMode::Auto:
    default:
        if (resolution.indirectLooksValid) {
            resolution.selected = resolution.indirect;
            resolution.effectiveMode = LobbyManagerAddressMode::Indirect;
        }
        else if (resolution.directLooksValid) {
            resolution.selected = resolution.direct;
            resolution.effectiveMode = LobbyManagerAddressMode::Direct;
        }
        break;
    }

    return resolution;
}

const char* PtrText(const void* ptr) {
    return ptr ? "valid" : "null";
}

bool ShouldCaptureOverlayInput() {
    return g_visible;
}

void ValueRow(const char* label, const char* value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value);
}

void PointerRow(const char* label, const void* value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "0x%p", value);
    ValueRow(label, buffer);
}

void IntRow(const char* label, int value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%d", value);
    ValueRow(label, buffer);
}

void UIntRow(const char* label, uint32_t value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%u (0x%X)", value, value);
    ValueRow(label, buffer);
}

void FloatRow(const char* label, float value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.3f", value);
    ValueRow(label, buffer);
}

int SafeEntryCount(const Game::CharacterEntry4240* begin, const Game::CharacterEntry4240* end) {
    if (!begin || !end || end < begin) {
        return 0;
    }
    return static_cast<int>(end - begin);
}

int SafeCapacityCount(const Game::CharacterEntry4240* begin, const Game::CharacterEntry4240* capacityEnd) {
    if (!begin || !capacityEnd || capacityEnd < begin) {
        return 0;
    }
    return static_cast<int>(capacityEnd - begin);
}

int CountPopulatedWorkshopSaveSlots(const Game::LobbyManager* lobby) {
    if (!lobby) {
        return 0;
    }

    int count = 0;
    for (const auto& slot : lobby->workshopSaveSlots10) {
        if (slot.presenceValue != 0 || slot.workshopCharacterIdOrSentinel != -1) {
            ++count;
        }
    }
    return count;
}

bool BeginDebugPanel(const char* id, const char* title, ImVec2 size = ImVec2(0.0f, 0.0f)) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.33f, 0.37f, 0.42f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    const bool open = ImGui::BeginChild(id, size, true, ImGuiWindowFlags_None);
    ImGui::TextUnformatted(title);
    ImGui::Separator();
    return open;
}

void EndDebugPanel() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void DrawLobbyManagerSelectionState(const Game::LobbySelectionState* state) {
    if (!state) {
        ImGui::TextUnformatted("selectionState is null.");
        return;
    }

    if (ImGui::BeginTable("SelectionStateTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
        PointerRow("ownerOrContext", state->ownerOrContext);
        IntRow("field_04", state->field_04);
        IntRow("field_08", state->field_08);
        IntRow("field_0C", state->field_0C);
        IntRow("field_10", state->field_10);
        IntRow("field_14", state->field_14);
        IntRow("field_18", state->field_18);
        UIntRow("field_1C", state->field_1C);
        IntRow("field_20", state->field_20);
        IntRow("field_24", state->field_24);
        IntRow("field_28", state->field_28);
        IntRow("field_2C", state->field_2C);
        IntRow("field_30", state->field_30);
        UIntRow("field_34", state->field_34);
        IntRow("field_38", state->field_38);
        IntRow("currentVisibleSlot", state->currentVisibleSlot);
        UIntRow("field_40", state->field_40);
        IntRow("field_44", state->field_44);
        IntRow("field_48", state->field_48);
        IntRow("field_4C", state->field_4C);
        UIntRow("field_50", state->field_50);
        IntRow("field_54", state->field_54);
        UIntRow("field_58", state->field_58);
        IntRow("field_5C", state->field_5C);
        IntRow("field_60", state->field_60);
        UIntRow("field_64", state->field_64);
        UIntRow("field_84FC", state->field_84FC);
        IntRow("field_8500", state->field_8500);
        PointerRow("entries43[0]", &state->entries43[0]);
        PointerRow("entries43[last]", &state->entries43[42]);
        PointerRow("entries8[0]", &state->entries8[0]);
        PointerRow("entries8[last]", &state->entries8[7]);
        IntRow("field_861C", state->field_861C);
        IntRow("field_8620", state->field_8620);
        IntRow("field_8624", state->field_8624);
        IntRow("field_8628", state->field_8628);
        IntRow("field_862C", state->field_862C);
        IntRow("field_8630", state->field_8630);
        IntRow("field_8634", state->field_8634);
        IntRow("field_8638", state->field_8638);
        IntRow("field_863C", state->field_863C);
        IntRow("field_8640", state->field_8640);
        IntRow("field_8644", state->field_8644);
        IntRow("field_8648", state->field_8648);
        IntRow("field_864C", state->field_864C);
        IntRow("field_2192", state->field_2192);
        IntRow("field_2193", state->field_2193);
        IntRow("ptr_2073", state->ptr_2073);
        IntRow("ptr_2074", state->ptr_2074);
        IntRow("field_2071", state->field_2071);
        IntRow("field_2072", state->field_2072);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Text("smallBuf_8524: %.*s", static_cast<int>(sizeof(state->smallBuf_8524)), state->smallBuf_8524);
    ImGui::Text("fourccLike_8608: %.*s", static_cast<int>(sizeof(state->fourccLike_8608)), state->fourccLike_8608);

    if (ImGui::CollapsingHeader("helperObjects4", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < 4; ++i) {
            ImGui::BulletText("[%d] %d", i, state->helperObjects4[i]);
        }
    }
    if (ImGui::CollapsingHeader("helperObjects8", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < 8; ++i) {
            ImGui::BulletText("[%d] %d", i, state->helperObjects8[i]);
        }
    }
}

void DrawOwnedBufferTable(const char* tableId, const char* name, const Game::OwnedBuffer20& buffer) {
    if (ImGui::BeginTable(tableId, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
        PointerRow("heapData", buffer.heapData);
        UIntRow("field_04", buffer.field_04);
        UIntRow("length", buffer.length);
        UIntRow("capacity", buffer.capacity);
        UIntRow("inlineThreshold", buffer.inlineThreshold);
        ImGui::EndTable();
    }
    ImGui::Spacing();
    ImGui::Text("%s", name);
}

void DrawOwnedBufferInline(const char* label, const Game::OwnedBuffer20& buffer) {
    if (ImGui::TreeNode(label)) {
        if (ImGui::BeginTable(label, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
            PointerRow("heapData", buffer.heapData);
            UIntRow("field_04", buffer.field_04);
            UIntRow("length", buffer.length);
            UIntRow("capacity", buffer.capacity);
            UIntRow("inlineThreshold", buffer.inlineThreshold);
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }
}

void DrawCharacterRuntimeHeaderPreview(const Game::CharacterEntry4240* entry) {
    const auto* runtime = GetCharacterRuntimeHeader(entry);
    if (!runtime) {
        return;
    }

    const auto* entryBytes = reinterpret_cast<const uint8_t*>(entry);
    const auto runtimeField216 = *reinterpret_cast<const uint32_t*>(entryBytes + 0xD8);
    const auto runtimeField232 = *reinterpret_cast<const uint32_t*>(entryBytes + 0xE8);

    if (ImGui::TreeNode("runtimeHeader@+0x10")) {
        if (ImGui::BeginTable("CharacterRuntimeHeaderPreviewTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
            UIntRow("activeFlag@+0x10", runtime->activeFlag);
            UIntRow("field_08@+0x18", runtime->field_08);
            UIntRow("field_0C@+0x1C", runtime->field_0C);
            UIntRow("field_10@+0x20", runtime->field_10);
            IntRow("workshopCharacterIdOrSentinel@+0x24", runtime->workshopCharacterIdOrSentinel);
            UIntRow("workshopIdentityLo@+0x28", runtime->workshopIdentityLo);
            UIntRow("workshopIdentityHi@+0x2C", runtime->workshopIdentityHi);
            UIntRow("runtimeField216@+0xD8", runtimeField216);
            UIntRow("runtimeField232@+0xE8", runtimeField232);
            ValueRow("hasWorkshopCharacter", runtime->workshopCharacterIdOrSentinel != -1 ? "true" : "false");
            ValueRow("hasIdentity", (runtime->workshopIdentityLo != 0 || runtime->workshopIdentityHi != 0) ? "true" : "false");
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }
}

void DrawCharacterNodePreview(const Game::CharacterNode200& node) {
    if (ImGui::TreeNode("node28")) {
        if (ImGui::BeginTable("CharacterNodePreviewTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
            UIntRow("dword_18", node.dword_18);
            UIntRow("dword_1C", node.dword_1C);
            UIntRow("flag_34", node.flag_34);
            UIntRow("flag_35", node.flag_35);
            UIntRow("dword_AC", node.dword_AC);
            IntRow("sentinel_B0", node.sentinel_B0);
            UIntRow("dword_B8", node.dword_B8);
            UIntRow("dword_BC", node.dword_BC);
            UIntRow("dword_C0", node.dword_C0);
            UIntRow("flag_C4", node.flag_C4);
            UIntRow("flag_C5", node.flag_C5);
            ImGui::EndTable();
        }
        DrawOwnedBufferInline("node28.buffer0", node.buffer0);
        DrawOwnedBufferInline("node28.buffer1", node.buffer1);
        ImGui::TreePop();
    }
}

void DrawCharacterEntryPreview(const Game::CharacterEntry4240* entry, const char* title) {
    if (!entry || !LooksLikeCharacterEntry(entry)) {
        ImGui::TextUnformatted("Character entry unavailable.");
        return;
    }

    if (!BeginDebugPanel(title, title, ImVec2(0.0f, 0.0f))) {
        EndDebugPanel();
        return;
    }

    if (ImGui::BeginTable(title, 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
        UIntRow("field_00", entry->field_00);
        IntRow("workshopCharacterIdOrSentinel", entry->workshopCharacterIdOrSentinel);
        UIntRow("workshopIdentityLo", entry->workshopIdentityLo);
        UIntRow("workshopIdentityHi", entry->workshopIdentityHi);
        ValueRow("hasWorkshopCharacter", entry->workshopCharacterIdOrSentinel != -1 ? "true" : "false");
        ValueRow("hasIdentity", (entry->workshopIdentityLo != 0 || entry->workshopIdentityHi != 0) ? "true" : "false");
        ImGui::EndTable();
    }

    ImGui::Spacing();
    DrawOwnedBufferInline("buffer08", entry->buffer08);
    DrawCharacterRuntimeHeaderPreview(entry);
    DrawCharacterNodePreview(entry->node28);

    EndDebugPanel();
}

void DrawLobbyManagerKnownSummary(const Game::LobbyManager* lobby) {
    if (!lobby || !LooksLikeLobbyManager(lobby)) {
        ImGui::TextUnformatted("Resolved LobbyManager does not look valid enough for interpreted rendering.");
        return;
    }

    const int entryCount = SafeEntryCount(lobby->characterArrayBegin, lobby->characterArrayEnd);
    const int capacityCount = SafeCapacityCount(lobby->characterArrayBegin, lobby->characterArrayCapacityEnd);
    const int populatedWorkshopSaveSlots = CountPopulatedWorkshopSaveSlots(lobby);

    if (BeginDebugPanel("LobbyManagerKnownSummary", "LobbyManager Known Fields", ImVec2(0.0f, 240.0f))) {
        if (ImGui::BeginTable("LobbyManagerKnownSummaryTable", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
            PointerRow("characterArrayBegin", lobby->characterArrayBegin);
            PointerRow("characterArrayEnd", lobby->characterArrayEnd);
            PointerRow("characterArrayCapacityEnd", lobby->characterArrayCapacityEnd);
            IntRow("characterArrayEndCount (lazy)", entryCount);
            IntRow("characterArrayCapacity", capacityCount);
            PointerRow("currentCharacterBase", lobby->currentCharacterBase);
            PointerRow("currentCharacterPtr", lobby->currentCharacterPtr);
            PointerRow("selectionState", lobby->selectionState);
            UIntRow("selectionEnabled", lobby->selectionEnabled);
            UIntRow("field_14664", lobby->field_14664);
            UIntRow("field_14668", lobby->field_14668);
            UIntRow("skinScaleOrWidth", lobby->skinScaleOrWidth);
            PointerRow("workshopRuntime", lobby->workshopRuntime);
            UIntRow("field_14680", lobby->field_14680);
            UIntRow("workshopSyncBusy", lobby->workshopSyncBusy);
            UIntRow("workshopArrayValid", lobby->workshopArrayValid);
            IntRow("populatedWorkshopSaveSlots (live)", populatedWorkshopSaveSlots);
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::TextUnformatted("characterArrayEndCount updates when the CharacterEntry vector catches up.");
        ImGui::TextUnformatted("populatedWorkshopSaveSlots is the live workshop/save-side availability signal.");
    }
    EndDebugPanel();

    if (BeginDebugPanel("LobbyWorkshopSaveSlotsPanel", "Workshop Save Slots 32..41", ImVec2(0.0f, 260.0f))) {
        if (ImGui::BeginTable("LobbyWorkshopSaveSlotsTable", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, ImVec2(0.0f, 220.0f))) {
            ImGui::TableSetupColumn("Slot");
            ImGui::TableSetupColumn("Address");
            ImGui::TableSetupColumn("Id/Sentinel");
            ImGui::TableSetupColumn("Presence");
            ImGui::TableSetupColumn("Unlocked");
            ImGui::TableHeadersRow();
            for (int i = 0; i < 10; ++i) {
                const auto& slot = lobby->workshopSaveSlots10[i];
                const bool unlocked = slot.presenceValue != 0 || slot.workshopCharacterIdOrSentinel != -1;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%d", 32 + i);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("0x%p", static_cast<const void*>(&slot));
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", slot.workshopCharacterIdOrSentinel);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("0x%016llX", static_cast<unsigned long long>(slot.presenceValue));
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(unlocked ? "0x80" : "0x00");
            }
            ImGui::EndTable();
        }
    }
    EndDebugPanel();

    if (entryCount > 0 && lobby->characterArrayBegin) {
        if (g_characterEntryPreviewIndex >= entryCount) {
            g_characterEntryPreviewIndex = entryCount - 1;
        }
        if (g_characterEntryPreviewIndex < 0) {
            g_characterEntryPreviewIndex = 0;
        }

        ImGui::SliderInt("Character Entry Index", &g_characterEntryPreviewIndex, 0, entryCount - 1);
        const auto* indexedEntry = lobby->characterArrayBegin + g_characterEntryPreviewIndex;
        DrawCharacterEntryPreview(indexedEntry, "Character Entry Preview");
    }

    if (LooksLikeCharacterEntry(lobby->currentCharacterBase)) {
        DrawCharacterEntryPreview(lobby->currentCharacterBase, "Current Character Base");
    }

    if (LooksLikeCharacterEntry(lobby->currentCharacterPtr) && lobby->currentCharacterPtr != lobby->currentCharacterBase) {
        DrawCharacterEntryPreview(lobby->currentCharacterPtr, "Current Character Ptr");
    }
}

const char* AddressModeLabel(LobbyManagerAddressMode mode) {
    switch (mode) {
    case LobbyManagerAddressMode::Auto:
        return "Auto";
    case LobbyManagerAddressMode::Direct:
        return "Direct";
    case LobbyManagerAddressMode::Indirect:
        return "Indirect";
    default:
        return "Unknown";
    }
}

void DrawLobbyManagerDebug() {
    const LobbyManagerResolution resolution = ResolveLobbyManager();
    const void* dumpBaseAddress = nullptr;
    const char* dumpBaseLabel = nullptr;

    switch (g_lobbyManagerAddressMode) {
    case LobbyManagerAddressMode::Direct:
        dumpBaseAddress = reinterpret_cast<const void*>(resolution.storageAddress);
        dumpBaseLabel = "moduleBase + offset";
        break;
    case LobbyManagerAddressMode::Indirect:
        dumpBaseAddress = resolution.hasReadablePointerStorage ? static_cast<const void*>(resolution.indirect) : nullptr;
        dumpBaseLabel = "*reinterpret_cast<void**>(moduleBase + offset)";
        break;
    case LobbyManagerAddressMode::Auto:
    default:
        dumpBaseAddress = resolution.selected ? static_cast<const void*>(resolution.selected) : reinterpret_cast<const void*>(resolution.storageAddress);
        dumpBaseLabel = resolution.selected ? "auto-resolved target" : "moduleBase + offset";
        break;
    }

    ImGui::Text("LobbyManager offset: 0x%X", static_cast<unsigned>(Game::LOBBY_MANAGER_OFFSET));
    ImGui::Text("Storage address: 0x%p", reinterpret_cast<const void*>(resolution.storageAddress));
    ImGui::Text("Direct candidate: 0x%p", static_cast<const void*>(resolution.direct));
    ImGui::Text("Indirect candidate: 0x%p", static_cast<const void*>(resolution.indirect));
    ImGui::Text("Configured mode: %s", AddressModeLabel(g_lobbyManagerAddressMode));
    ImGui::Text("Resolved mode: %s", AddressModeLabel(resolution.effectiveMode));
    ImGui::Text("Dump source: %s", dumpBaseLabel ? dumpBaseLabel : "unavailable");
    ImGui::Text("Dump address: 0x%p", dumpBaseAddress);
    ImGui::Separator();
    if (resolution.selected) {
        DrawLobbyManagerKnownSummary(resolution.selected);
    }
}

void SetOverlayInputCapture(bool capture) {
    if (!g_initialized || !g_gameWindow || g_inputCaptured == capture) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = capture;

    if (capture) {
        SetForegroundWindow(g_gameWindow);
        SetFocus(g_gameWindow);
        SetCapture(g_gameWindow);
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) < 0) {}
    }
    else {
        if (GetCapture() == g_gameWindow) {
            ReleaseCapture();
        }
        while (ShowCursor(FALSE) >= 0) {}
        io.MouseDrawCursor = false;
    }

    g_inputCaptured = capture;
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ShouldCaptureOverlayInput()) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);

        switch (msg) {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            return true;
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
            if (!g_keyboardPassthrough) {
                return true;
            }
            break;
        default:
            break;
        }
    }
    return CallWindowProc(g_originalWndProc, hwnd, msg, wParam, lParam);
}

void DrawHookCrashersTab() {
    g_activeTab = OverlayTab::HookCrashers;
    ImGui::Text("Module base: 0x%p", reinterpret_cast<void*>(g_moduleBase));
    ImGui::Text("Overlay toggle VK: 0x%X", Config::HookCrashersConfig::Instance().Get().overlayToggleVirtualKey);
    ImGui::Separator();
    ImGui::Text("Save expansion: always enabled");
    ImGui::Text("Custom localizations: %s", Config::HookCrashersConfig::Instance().Get().enableCustomLocalizations ? "enabled" : "disabled");
}

void DrawModsTab() {
    g_activeTab = OverlayTab::Mods;
    const size_t count = Core::ModLoader::GetModCount();
    ImGui::Text("Loaded mods: %u", static_cast<unsigned>(count));
    ImGui::Separator();
    for (size_t i = 0; i < count; ++i) {
        const Core::ModInfo* info = Core::ModLoader::GetModInfoByIndex(i);
        if (!info) continue;
        ImGui::Text("%u. %s", static_cast<unsigned>(i + 1), info->name.c_str());
        ImGui::Text("   Author: %s", info->author.c_str());
        ImGui::Text("   Version: %s", info->version.c_str());
        ImGui::Text("   Type: %s", info->isScriptMod ? "Folder Mod" : "Binary Mod");
        if (!info->path.empty()) {
            ImGui::Text("   Path: %s", info->path.c_str());
        }
        if (info->isScriptMod) {
            ImGui::Text("   main.lua: %s", info->hasMainLua ? "yes" : "no");
            ImGui::Text("   locs.json: %s", info->hasLocalizations ? "yes" : "no");
            ImGui::Text("   manifest.json: %s", info->hasManifest ? "yes" : "no");
            ImGui::Text("   icon.png: %s", info->hasIcon ? "yes" : "no");
            if (info->hasIcon && !info->iconPath.empty()) {
                ImGui::Text("   Icon path: %s", info->iconPath.c_str());
            }
        }
    }
}

void DrawLogsTab() {
    g_activeTab = OverlayTab::Logs;
    static bool autoScroll = true;
    static int selectedScope = 0;

    ImGui::Checkbox("Auto-scroll", &autoScroll);
    ImGui::Separator();

    const auto entries = Util::Logger::Instance().GetBufferedEntries();
    std::vector<std::string> scopes;
    scopes.emplace_back("All");
    {
        std::set<std::string> uniqueScopes;
        for (const auto& entry : entries) {
            if (!entry.scope.empty()) {
                uniqueScopes.insert(entry.scope);
            }
        }
        for (const auto& scope : uniqueScopes) {
            scopes.push_back(scope);
        }
    }
    if (selectedScope >= static_cast<int>(scopes.size())) {
        selectedScope = 0;
    }

    ImGui::Text("Scope");
    ImGui::SameLine();
    if (ImGui::BeginCombo("##LogScope", scopes[selectedScope].c_str())) {
        for (int i = 0; i < static_cast<int>(scopes.size()); ++i) {
            const bool isSelected = selectedScope == i;
            if (ImGui::Selectable(scopes[i].c_str(), isSelected)) {
                selectedScope = i;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Separator();

    ImGui::BeginChild("HookCrashersLogs", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& entry : entries) {
        if (selectedScope > 0 && entry.scope != scopes[selectedScope]) {
            continue;
        }
        ImVec4 color(1.0f, 1.0f, 1.0f, 1.0f);
        if (entry.level == "warning") color = ImVec4(1.0f, 0.78f, 0.25f, 1.0f);
        else if (entry.level == "error" || entry.level == "critical") color = ImVec4(1.0f, 0.35f, 0.30f, 1.0f);
        else if (entry.level == "debug" || entry.level == "trace") color = ImVec4(0.65f, 0.75f, 0.85f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        if (!entry.scope.empty()) {
            ImGui::Text("[%s] %s", entry.scope.c_str(), entry.message.c_str());
        }
        else {
            ImGui::TextUnformatted(entry.message.c_str());
        }
        ImGui::PopStyleColor();
    }
    if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void DrawAboutTab() {
    g_activeTab = OverlayTab::About;
    ImGui::Text("HookCrashers");
    ImGui::Text("Castle Crashers ASI modding API");
    ImGui::Separator();
    ImGui::Text("Built-in systems: save expansion, localization, mod loader, SWF dispatch, logging.");
}

void RenderOverlay() {
    bool wasVisible = g_visible;
    g_keyboardPassthrough = false;
    ImGui::SetNextWindowSize(ImVec2(1360, 920), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("HookCrashers", &g_visible)) {
        ImGui::End();
        if (wasVisible != g_visible) {
            SetOverlayInputCapture(g_visible);
        }
        return;
    }

    if (ImGui::BeginTabBar("HookCrashersTabs")) {
        if (ImGui::BeginTabItem("HookCrashers")) {
            DrawHookCrashersTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Mods")) {
            DrawModsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Logs")) {
            DrawLogsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("About")) {
            DrawAboutTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
    if (wasVisible != g_visible) {
        SetOverlayInputCapture(ShouldCaptureOverlayInput());
    }
}

bool InitializeImGui(IDirect3DDevice9* device) {
    if (g_initialized) return true;

    D3DDEVICE_CREATION_PARAMETERS params{};
    if (FAILED(device->GetCreationParameters(&params))) {
        Util::Logger::Instance().Get()->error("[Overlay] Failed to read D3D9 creation parameters.");
        return false;
    }

    g_gameWindow = params.hFocusWindow;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplWin32_Init(g_gameWindow);
    ImGui_ImplDX9_Init(device);

    g_originalWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtr(g_gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(OverlayWndProc)));

    g_initialized = true;
    Util::Logger::Instance().Get()->info("[Overlay] ImGui DX9 overlay initialized.");
    return true;
}

HRESULT __stdcall HookedReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params) {
    if (g_initialized) {
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }
    HRESULT result = g_originalReset(device, params);
    if (g_initialized && SUCCEEDED(result)) {
        ImGui_ImplDX9_CreateDeviceObjects();
    }
    return result;
}

void RunOverlayFrame(IDirect3DDevice9* device) {
    if (!g_initialized) {
        InitializeImGui(device);
    }

    const int toggleKey = Config::HookCrashersConfig::Instance().Get().overlayToggleVirtualKey;
    const bool toggleDown = (GetAsyncKeyState(toggleKey) & 0x8000) != 0;
    if (toggleDown && !g_lastToggleDown) {
        g_visible = !g_visible;
        SetOverlayInputCapture(ShouldCaptureOverlayInput());
    }
    g_lastToggleDown = toggleDown;

    if (g_initialized && g_visible) {
        SetOverlayInputCapture(ShouldCaptureOverlayInput());
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        RenderOverlay();
        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }
    g_lastFrameTick = GetTickCount();
}

HRESULT __stdcall HookedEndScene(IDirect3DDevice9* device) {
    RunOverlayFrame(device);

    return g_originalEndScene(device);
}

HRESULT __stdcall HookedPresent(IDirect3DDevice9* device, const RECT* sourceRect, const RECT* destRect, HWND destWindowOverride, const RGNDATA* dirtyRegion) {
    if (GetTickCount() - g_lastFrameTick > 8) {
        RunOverlayFrame(device);
    }

    return g_originalPresent(device, sourceRect, destRect, destWindowOverride, dirtyRegion);
}

bool ResolveD3D9VTable(void** endScene, void** present, void** reset) {
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "HookCrashersD3D9Dummy";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowA(wc.lpszClassName, "HookCrashers", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d || !hwnd) {
        if (d3d) d3d->Release();
        if (hwnd) DestroyWindow(hwnd);
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        return false;
    }

    D3DPRESENT_PARAMETERS params{};
    params.Windowed = TRUE;
    params.SwapEffect = D3DSWAPEFFECT_DISCARD;
    params.hDeviceWindow = hwnd;

    IDirect3DDevice9* device = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &params, &device);
    if (FAILED(hr)) {
        d3d->Release();
        DestroyWindow(hwnd);
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        return false;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    *reset = vtable[16];
    *present = vtable[17];
    *endScene = vtable[42];

    device->Release();
    d3d->Release();
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return true;
}

} // namespace

bool InitializeOverlay() {
    if (!Config::HookCrashersConfig::Instance().Get().enableOverlay) {
        Util::Logger::Instance().Get()->info("[Overlay] Disabled by config.");
        return true;
    }

    void* endScene = nullptr;
    void* present = nullptr;
    void* reset = nullptr;
    if (!ResolveD3D9VTable(&endScene, &present, &reset)) {
        Util::Logger::Instance().Get()->error("[Overlay] Failed to resolve D3D9 vtable.");
        return false;
    }

    g_originalEndScene = reinterpret_cast<EndScene_t>(endScene);
    g_originalPresent = reinterpret_cast<Present_t>(present);
    g_originalReset = reinterpret_cast<Reset_t>(reset);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)g_originalEndScene, HookedEndScene);
    DetourAttach(&(PVOID&)g_originalPresent, HookedPresent);
    DetourAttach(&(PVOID&)g_originalReset, HookedReset);
    if (DetourTransactionCommit() != NO_ERROR) {
        Util::Logger::Instance().Get()->error("[Overlay] Failed to attach D3D9 detours.");
        return false;
    }

    Util::Logger::Instance().Get()->info("[Overlay] D3D9 hooks attached.");
    return true;
}

void ShutdownOverlay() {
    SetOverlayInputCapture(false);
    if (g_originalWndProc && g_gameWindow) {
        SetWindowLongPtr(g_gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_originalWndProc));
    }
    if (g_initialized) {
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_initialized = false;
    }
}

bool IsOverlayVisible() {
    return g_visible;
}

} // namespace UI
} // namespace HookCrashers
