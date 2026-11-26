#include "SaveSlots.h"

#include "SaveCrypto.h"
#include "SaveFileRedirect.h"
#include "SaveLayout.h"
#include "../Characters/CharacterExpansion.h"
#include "../Characters/CharacterRegistry.h"
#include "../Core/Logger.h"
#include "../Core/Memory.h"
#include "../Core/ModLoader.h"
#include "../Core/Version.h"

#include <windows.h>
#include <detours.h>

#include <array>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <optional>

// RVAs belong to castle.exe build 0x6A4D42BF (2026-07-07); see DOCUMENTAZIONE-RE.md
// "Salvataggio" and re-archive/analysis/decomp-save.
namespace HookCrashers::Save::Slots {
namespace {

constexpr std::uintptr_t kReadSaveFileRva = 0x0E8E50;   // stdcall(Result* out, int player), ret 8
constexpr std::uintptr_t kWriteSaveFileRva = 0x0E9280;  // stdcall(Result* out, int player), ret 8
constexpr std::uintptr_t kSaveManagerPointerRva = 0x391280; // -> manager; +0x34 -> save object {+4 buffer, +8 payload size}
constexpr std::uintptr_t kBlowfishInitRva = 0x1C36A0;   // P[18] followed by S[4][256]
constexpr std::uintptr_t kBaseKeyRva = 0x1C4700;        // 24 bytes, SteamID written over it
constexpr std::uint32_t kBlowfishP0 = 0x243F6A88u;      // first P word (pi), sanity check

constexpr const char* kVanillaSaveName = "cc_save.dat";
constexpr const char* kSlotConfigName = "hc_slots.cfg";
constexpr const char* kPbpName = "hcpbp.dat";
constexpr std::uint8_t kSentinel = 0xEE;

using SaveIoFn = void(__stdcall*)(std::uint32_t* result, int player);

// --- Steam Remote Storage (flat API of the game's steam_api.dll) ----------------
class Storage {
public:
    bool Bind() {
        if (m_storage) return true;
        HMODULE steam = GetModuleHandleA("steam_api.dll");
        if (!steam) return false;
        auto get = [&](const char* name) { return GetProcAddress(steam, name); };
        auto storage = reinterpret_cast<void*(__cdecl*)()>(get("SteamAPI_SteamRemoteStorage_v016"));
        auto user = reinterpret_cast<void*(__cdecl*)()>(get("SteamAPI_SteamUser_v023"));
        m_write = reinterpret_cast<bool(__cdecl*)(void*, const char*, const void*, std::int32_t)>(get("SteamAPI_ISteamRemoteStorage_FileWrite"));
        m_read = reinterpret_cast<std::int32_t(__cdecl*)(void*, const char*, void*, std::int32_t)>(get("SteamAPI_ISteamRemoteStorage_FileRead"));
        m_exists = reinterpret_cast<bool(__cdecl*)(void*, const char*)>(get("SteamAPI_ISteamRemoteStorage_FileExists"));
        m_size = reinterpret_cast<std::int32_t(__cdecl*)(void*, const char*)>(get("SteamAPI_ISteamRemoteStorage_GetFileSize"));
        m_delete = reinterpret_cast<bool(__cdecl*)(void*, const char*)>(get("SteamAPI_ISteamRemoteStorage_FileDelete"));
        auto steamId = reinterpret_cast<std::uint64_t(__cdecl*)(void*)>(get("SteamAPI_ISteamUser_GetSteamID"));
        if (!storage || !user || !m_write || !m_read || !m_exists || !m_size || !m_delete || !steamId) return false;
        void* userObject = user();
        m_storage = storage();
        if (!m_storage || !userObject) {
            m_storage = nullptr;
            return false;
        }
        m_steamId = steamId(userObject);
        return m_steamId != 0;
    }

    bool Exists(const std::string& name) const { return m_storage && m_exists(m_storage, name.c_str()); }

    bool Read(const std::string& name, std::vector<std::uint8_t>& out) const {
        out.clear();
        if (!Exists(name)) return false;
        const std::int32_t size = m_size(m_storage, name.c_str());
        if (size <= 0 || size > 1024 * 1024) return false;
        out.resize(static_cast<std::size_t>(size));
        if (m_read(m_storage, name.c_str(), out.data(), size) != size) {
            out.clear();
            return false;
        }
        return true;
    }

    bool Write(const std::string& name, const void* data, std::size_t size) const {
        return m_storage && size > 0 && m_write(m_storage, name.c_str(), data, static_cast<std::int32_t>(size));
    }
    bool Write(const std::string& name, const std::vector<std::uint8_t>& data) const { return Write(name, data.data(), data.size()); }
    bool Write(const std::string& name, const std::string& text) const { return Write(name, text.data(), text.size()); }
    bool Delete(const std::string& name) const { return m_storage && m_delete(m_storage, name.c_str()); }
    std::uint64_t SteamId() const { return m_steamId; }

private:
    void* m_storage = nullptr;
    std::uint64_t m_steamId = 0;
    bool(__cdecl* m_write)(void*, const char*, const void*, std::int32_t) = nullptr;
    std::int32_t(__cdecl* m_read)(void*, const char*, void*, std::int32_t) = nullptr;
    bool(__cdecl* m_exists)(void*, const char*) = nullptr;
    std::int32_t(__cdecl* m_size)(void*, const char*) = nullptr;
    bool(__cdecl* m_delete)(void*, const char*) = nullptr;
};

// --- State (game thread after Install) -------------------------------------------
struct SlotState {
    bool used = false;
    bool hasMeta = false;
    SlotMeta meta;
};

Storage g_storage;
SaveIoFn g_originalRead = nullptr;
SaveIoFn g_originalWrite = nullptr;
bool g_installed = false;
bool g_ready = false;
bool g_initTried = false;
bool g_inSwitch = false;
std::string g_legacyManifest;
int g_active = 1;
std::array<SlotState, kMaxSlots + 1> g_slots; // 1-based
bool g_modsRecorded = false;                   // active slot meta refreshed this session
std::int64_t g_lastMetaWrite = 0;
void (*g_changed)() = nullptr;
// Mods the active slot was last played with, captured before this session
// overwrites them, so Mod Info still shows what changed since then.
std::vector<ModEntry> g_activeSavedMods;

std::int64_t Now() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

bool ValidIndex(int index) {
    return index >= 1 && index <= kMaxSlots;
}

std::vector<std::string> CurrentKeys() {
    std::vector<std::string> names;
    for (const auto& addon : Characters::Registry::Instance().Addons()) names.push_back(addon.name);
    return Layout::MakeKeys(names);
}

bool CryptoTables(const std::uint32_t*& tables, const std::uint8_t*& baseKey) {
    tables = reinterpret_cast<const std::uint32_t*>(Core::Memory::Va(kBlowfishInitRva));
    baseKey = reinterpret_cast<const std::uint8_t*>(Core::Memory::Va(kBaseKeyRva));
    if (!tables || tables[0] != kBlowfishP0) {
        Core::Logger::Error("SaveSlots: Blowfish tables not found at rva=0x%X", static_cast<unsigned>(kBlowfishInitRva));
        return false;
    }
    return true;
}

bool Decrypt(std::vector<std::uint8_t>& data) {
    const std::uint32_t* tables = nullptr;
    const std::uint8_t* baseKey = nullptr;
    if (!CryptoTables(tables, baseKey)) return false;
    Crypto::Decrypt(data, tables, Crypto::DeriveKey(baseKey, g_storage.SteamId()));
    return true;
}

bool Encrypt(std::vector<std::uint8_t>& data) {
    const std::uint32_t* tables = nullptr;
    const std::uint8_t* baseKey = nullptr;
    if (!CryptoTables(tables, baseKey)) return false;
    Crypto::Encrypt(data, tables, Crypto::DeriveKey(baseKey, g_storage.SteamId()));
    return true;
}

// Decrypted, checksum-valid, current-version save.
bool ReadPlain(const std::string& name, std::vector<std::uint8_t>& plain, std::string& error) {
    if (!g_storage.Read(name, plain)) {
        error = "cannot read " + name;
        return false;
    }
    if (!Decrypt(plain)) {
        error = "save encryption tables unavailable";
        return false;
    }
    if (!Crypto::HasValidChecksum(plain)) {
        error = name + " has an invalid checksum (other Steam account or damaged file)";
        return false;
    }
    if (plain[Crypto::kVersionOffset] != Crypto::kCurrentSaveVersion) {
        error = name + " uses an old save format; open it once in the vanilla game first";
        return false;
    }
    return true;
}

bool WritePlain(const std::string& name, std::vector<std::uint8_t> plain) {
    Crypto::StoreChecksum(plain);
    return Encrypt(plain) && g_storage.Write(name, plain);
}

// Character list of a save written before slots existed: assume the current
// mods in their current order, truncated or extended to the file's size.
std::vector<std::string> InferredKeys(std::size_t fileSize) {
    const int count = Layout::AddonCountForSize(fileSize);
    std::vector<std::string> keys = CurrentKeys();
    if (count < 0) return keys;
    keys.resize(static_cast<std::size_t>(count));
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].empty()) keys[i] = "unknown#" + std::to_string(i + 1);
    }
    return Layout::MakeKeys(keys);
}

bool SaveMeta(int index) {
    SlotState& slot = g_slots[index];
    slot.meta.index = index;
    if (!g_storage.Write(MetaFileName(index), Serialize(slot.meta))) {
        Core::Logger::Error("SaveSlots: cannot write %s", MetaFileName(index).c_str());
        return false;
    }
    slot.hasMeta = true;
    return true;
}

std::string DefaultName(int index) {
    return "Slot " + std::to_string(index);
}

void LoadSlot(int index) {
    SlotState& slot = g_slots[index];
    slot = {};
    std::vector<std::uint8_t> text;
    if (g_storage.Read(MetaFileName(index), text)) {
        SlotMeta meta;
        if (Parse(std::string(text.begin(), text.end()), meta) && meta.index == index) {
            slot.meta = std::move(meta);
            slot.hasMeta = true;
        } else {
            Core::Logger::Warn("SaveSlots: %s is unreadable; slot metadata rebuilt", MetaFileName(index).c_str());
        }
    }
    slot.used = g_storage.Exists(SaveFileName(index));
    if (!slot.hasMeta) {
        slot.meta.index = index;
        slot.meta.name = DefaultName(index);
    }
}

void WriteConfig() {
    const std::string text = "active=" + std::to_string(g_active) + "\n";
    if (!g_storage.Write(kSlotConfigName, text)) Core::Logger::Warn("SaveSlots: cannot write %s", kSlotConfigName);
}

int ReadConfiguredSlot() {
    std::vector<std::uint8_t> text;
    if (!g_storage.Read(kSlotConfigName, text)) return 0;
    const std::string value(text.begin(), text.end());
    const std::size_t at = value.find("active=");
    if (at == std::string::npos) return 0;
    const int index = std::atoi(value.c_str() + at + 7);
    return ValidIndex(index) ? index : 0;
}

bool CopyStorageFile(const std::string& from, const std::string& to) {
    std::vector<std::uint8_t> bytes;
    return g_storage.Read(from, bytes) && g_storage.Write(to, bytes);
}

// First launch with slots: the save HookCrashers used until 5.2.0
// (hc_<first manifest>.dat) becomes slot 1, untouched.
void ImportLegacySave() {
    if (g_legacyManifest.empty()) return;
    const std::string legacy = SaveFileRedirect::LegacyManifestSaveName(g_legacyManifest);
    std::vector<std::uint8_t> raw;
    if (!g_storage.Read(legacy, raw)) return;
    if (!g_storage.Write(SaveFileName(1), raw)) {
        Core::Logger::Error("SaveSlots: could not copy %s to slot 1", legacy.c_str());
        return;
    }
    CopyStorageFile(legacy + ".bak", BackupFileName(1));
    SlotState& slot = g_slots[1];
    slot.used = true;
    slot.meta = {};
    slot.meta.name = g_legacyManifest;
    slot.meta.created = Now();
    slot.meta.characters = InferredKeys(raw.size());
    slot.meta.layoutKnown = false;
    SaveMeta(1);
    Core::Logger::Info("SaveSlots: imported %s (%u bytes) into slot 1; the original file is kept",
                       legacy.c_str(), static_cast<unsigned>(raw.size()));

    const std::string legacyPbp = SaveFileRedirect::LegacyManifestPbpName(g_legacyManifest);
    if (!g_storage.Exists(kPbpName) && CopyStorageFile(legacyPbp, kPbpName)) {
        Core::Logger::Info("SaveSlots: copied %s to %s", legacyPbp.c_str(), kPbpName);
    }
}

bool Initialize() {
    if (g_initTried) return g_ready;
    g_initTried = true;
    if (!g_storage.Bind()) {
        Core::Logger::Error("SaveSlots: Steam Remote Storage unavailable; staying on %s",
                            SaveFileRedirect::SaveFileName().c_str());
        return false;
    }
    bool anySlot = false;
    for (int index = 1; index <= kMaxSlots; ++index) {
        LoadSlot(index);
        anySlot = anySlot || g_slots[index].used || g_slots[index].hasMeta;
    }
    const int configured = ReadConfiguredSlot();
    if (!anySlot && configured == 0) {
        ImportLegacySave();
    }
    g_active = configured ? configured : 1;
    if (g_slots[g_active].used && IsBlocked(CompareMods(g_slots[g_active].meta.mods, CurrentMods()))) {
        // Never load (or remap) a slot whose character mods are gone or changed:
        // fall back to another playable slot, or an empty one, and leave it untouched.
        const int blocked = g_active;
        int fallback = 0;
        for (int index = 1; index <= kMaxSlots && !fallback; ++index) {
            if (g_slots[index].used && !IsBlocked(CompareMods(g_slots[index].meta.mods, CurrentMods()))) fallback = index;
        }
        for (int index = 1; index <= kMaxSlots && !fallback; ++index) {
            if (!g_slots[index].used) fallback = index;
        }
        if (!fallback) {
            Core::Logger::Error("SaveSlots: every slot is locked by missing character mods; save slots disabled");
            SaveFileRedirect::SetSlotFileNames("hc_locked.dat", "hc_locked.dat.bak");
            return false;
        }
        g_active = fallback;
        Core::Logger::Warn("SaveSlots: slot %d is locked (character mods missing or changed); starting on slot %d",
                           blocked, g_active);
    }
    g_activeSavedMods = g_slots[g_active].meta.mods;
    WriteConfig();
    if (!SaveFileRedirect::SetSlotFileNames(SaveFileName(g_active), BackupFileName(g_active))) {
        return false;
    }
    g_ready = true;
    Core::Logger::Info("SaveSlots: ready, active slot %d (%s)", g_active, g_slots[g_active].used ? "existing" : "new");
    return true;
}

// Brings slot `index` to the current character layout before the game reads it.
bool Prepare(int index, std::vector<std::uint8_t>* expected, std::string& error) {
    SlotState& slot = g_slots[index];
    const std::vector<std::string> current = CurrentKeys();
    if (expected) expected->clear();
    if (!g_storage.Exists(SaveFileName(index))) {
        slot.used = false;
        if (!slot.hasMeta || slot.meta.characters != current || !slot.meta.layoutKnown) {
            slot.meta.characters = current;
            slot.meta.layoutKnown = true;
            if (slot.meta.name.empty()) slot.meta.name = DefaultName(index);
        }
        return true;
    }
    slot.used = true;

    std::vector<std::uint8_t> plain;
    if (!ReadPlain(SaveFileName(index), plain, error)) {
        return false;
    }
    std::vector<std::string> previous = slot.meta.layoutKnown ? slot.meta.characters : InferredKeys(plain.size());
    if (Layout::SizeFor(previous.size()) != plain.size()) {
        Core::Logger::Warn("SaveSlots: slot %d metadata lists %u characters but the file holds %d; using the file size",
                           index, static_cast<unsigned>(previous.size()), Layout::AddonCountForSize(plain.size()));
        previous = InferredKeys(plain.size());
    }
    if (previous == current) {
        if (!slot.meta.layoutKnown) {
            slot.meta.characters = current;
            slot.meta.layoutKnown = true;
            SaveMeta(index);
        }
        if (expected) *expected = std::move(plain);
        return true;
    }

    std::vector<std::uint8_t> remapped;
    Layout::RemapReport report;
    const bool ok = Layout::Remap(plain, previous, current, slot.meta.stash,
        [](std::size_t addon, std::uint8_t* character) {
            Characters::InitializeAddonCharacter(character, static_cast<int>(addon));
        },
        remapped, report, error);
    if (!ok) return false;

    // Keep the pre-remap file next to the slot until the next remap.
    CopyStorageFile(SaveFileName(index), SaveFileName(index) + ".old");
    if (!WritePlain(SaveFileName(index), remapped)) {
        error = "cannot write the remapped save";
        return false;
    }
    slot.meta.characters = current;
    slot.meta.layoutKnown = true;
    SaveMeta(index);
    Core::Logger::Info("SaveSlots: slot %d remapped to %u addon characters (kept=%u restored=%u new=%u stashed=%u)",
                       index, static_cast<unsigned>(current.size()), static_cast<unsigned>(report.kept),
                       static_cast<unsigned>(report.restored), static_cast<unsigned>(report.created),
                       static_cast<unsigned>(report.stashed));
    if (expected) *expected = std::move(remapped);
    return true;
}

// Native in-memory save buffer (copied to by CC_ReadSaveFile, written by CC_WriteSaveFile).
std::uint8_t* GameSaveBuffer(std::size_t& size) {
    size = 0;
    const auto manager = *reinterpret_cast<std::uintptr_t*>(Core::Memory::Va(kSaveManagerPointerRva));
    if (!manager) return nullptr;
    const auto object = *reinterpret_cast<std::uintptr_t*>(manager + 0x34);
    if (!object) return nullptr;
    auto* buffer = *reinterpret_cast<std::uint8_t**>(object + 4);
    size = *reinterpret_cast<std::uint32_t*>(object + 8) + 4;
    return buffer;
}

void RefreshActiveMeta(bool force) {
    SlotState& slot = g_slots[g_active];
    const std::int64_t now = Now();
    const std::vector<ModEntry> mods = CurrentMods();
    const bool changed = !slot.hasMeta || !slot.used || slot.meta.mods != mods ||
                         slot.meta.hcVersion != Core::Version::kVersion || slot.meta.characters != CurrentKeys();
    if (!force && !changed && now - g_lastMetaWrite < 60) return;
    if (slot.meta.name.empty()) slot.meta.name = DefaultName(g_active);
    if (!slot.meta.created) slot.meta.created = now;
    slot.meta.played = now;
    slot.meta.mods = mods;
    slot.meta.hcVersion = Core::Version::kVersion;
    slot.meta.characters = CurrentKeys();
    slot.meta.layoutKnown = true;
    slot.used = true;
    if (SaveMeta(g_active)) g_lastMetaWrite = now;
}

void NotifyChanged() {
    if (g_changed) g_changed();
}

void __stdcall HookedRead(std::uint32_t* result, int player) {
    if (player == 0 && !g_inSwitch && Initialize()) {
        std::string error;
        if (!Prepare(g_active, nullptr, error)) {
            Core::Logger::Error("SaveSlots: slot %d could not be prepared: %s", g_active, error.c_str());
        }
        NotifyChanged();
    }
    g_originalRead(result, player);
}

void __stdcall HookedWrite(std::uint32_t* result, int player) {
    g_originalWrite(result, player);
    if (g_ready && !g_inSwitch && result && result[0] == 0) {
        const bool first = !g_modsRecorded;
        g_modsRecorded = true;
        RefreshActiveMeta(first);
        if (first) NotifyChanged();
    }
}

bool CallNative(SaveIoFn fn, const char* what) {
    std::uint32_t result[2] = {0xFFFFFFFFu, 0};
    fn(result, 0);
    if (result[0] != 0) {
        Core::Logger::Warn("SaveSlots: native %s returned %u", what, result[0]);
        return false;
    }
    return true;
}

} // namespace

bool Install(const std::string& legacyManifestName) {
    if (g_installed) return true;
    g_legacyManifest = legacyManifestName;
    if (!SaveFileRedirect::Install(SaveFileName(1), BackupFileName(1), kPbpName)) {
        return false;
    }
    g_originalRead = reinterpret_cast<SaveIoFn>(Core::Memory::Va(kReadSaveFileRva));
    g_originalWrite = reinterpret_cast<SaveIoFn>(Core::Memory::Va(kWriteSaveFileRva));
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<PVOID*>(&g_originalRead), HookedRead);
    DetourAttach(reinterpret_cast<PVOID*>(&g_originalWrite), HookedWrite);
    const LONG result = DetourTransactionCommit();
    if (result != NO_ERROR) {
        // The redirect alone is still safe: the game uses hc_slot1.dat and never cc_save.dat.
        Core::Logger::Error("SaveSlots: save detours failed (%ld); slot 1 only", result);
        return true;
    }
    g_installed = true;
    Core::Logger::Info("SaveSlots: installed (legacy save namespace '%s')", legacyManifestName.c_str());
    return true;
}

bool IsInstalled() {
    return g_installed;
}

bool IsReady() {
    return g_ready;
}

std::vector<ModEntry> CurrentMods() {
    std::vector<ModEntry> mods;
    for (const auto& mod : Core::ModLoader::LoadedMods()) mods.push_back({mod.name, mod.version, mod.characters > 0});
    return mods;
}

std::vector<SlotInfo> List() {
    std::vector<SlotInfo> list;
    if (!g_ready) return list;
    const std::vector<ModEntry> current = CurrentMods();
    for (int index = 1; index <= kMaxSlots; ++index) {
        const SlotState& slot = g_slots[index];
        SlotInfo info;
        info.index = index;
        info.used = slot.used;
        info.active = index == g_active;
        info.meta = slot.meta;
        if (info.meta.name.empty()) info.meta.name = DefaultName(index);
        if (slot.used) {
            info.mods = CompareMods(info.active ? g_activeSavedMods : slot.meta.mods, current);
            info.summary = Summarize(info.mods);
            info.blocked = !info.active && IsBlocked(info.mods);
        }
        list.push_back(std::move(info));
    }
    return list;
}

int ActiveSlot() {
    return g_active;
}

bool VanillaSaveAvailable() {
    return g_ready && g_storage.Exists(kVanillaSaveName);
}

bool Activate(int index, std::string& error) {
    if (!g_ready || !g_installed) {
        error = "save slots are not available";
        return false;
    }
    if (!ValidIndex(index)) {
        error = "invalid slot";
        return false;
    }
    if (index == g_active) return true;
    if (g_slots[index].used && IsBlocked(CompareMods(g_slots[index].meta.mods, CurrentMods()))) {
        error = "this slot needs mods that are missing or changed";
        return false;
    }

    std::size_t bufferSize = 0;
    std::uint8_t* buffer = GameSaveBuffer(bufferSize);
    const std::size_t expectedSize = Layout::SizeFor(CurrentKeys().size());
    if (!buffer || bufferSize != expectedSize) {
        error = "the game save buffer was not found";
        Core::Logger::Error("SaveSlots: save buffer %p size=0x%X expected=0x%X", buffer,
                            static_cast<unsigned>(bufferSize), static_cast<unsigned>(expectedSize));
        return false;
    }

    std::vector<std::uint8_t> expected;
    if (!Prepare(index, &expected, error)) return false;

    const int previous = g_active;
    const std::vector<std::uint8_t> snapshot(buffer, buffer + bufferSize);
    g_inSwitch = true;
    SaveFileRedirect::SetSlotFileNames(SaveFileName(index), BackupFileName(index));
    buffer[0] = kSentinel; // default creation and file reads both overwrite byte 0
    CallNative(g_originalRead, "read");
    bool loaded = buffer[0] != kSentinel;
    if (loaded && !expected.empty()) {
        loaded = expected.size() == bufferSize && std::memcmp(buffer, expected.data(), bufferSize - 4) == 0;
    }
    if (!loaded) {
        // A failed native read may have replaced the buffer with a default save:
        // put the previous slot's bytes back so its next autosave is unchanged.
        std::memcpy(buffer, snapshot.data(), bufferSize);
        SaveFileRedirect::SetSlotFileNames(SaveFileName(previous), BackupFileName(previous));
        g_inSwitch = false;
        error = "the game did not reload the save; still on slot " + std::to_string(previous);
        Core::Logger::Error("SaveSlots: switch %d -> %d not confirmed by the native read; reverted", previous, index);
        return false;
    }

    g_active = index;
    g_activeSavedMods = g_slots[index].meta.mods;
    g_modsRecorded = false;
    WriteConfig();
    const bool created = expected.empty();
    if (created) {
        // Persist the new default save so the slot exists even if nothing is played.
        if (CallNative(g_originalWrite, "write")) g_slots[index].used = true;
    }
    g_inSwitch = false;
    RefreshActiveMeta(true);
    g_modsRecorded = true;
    Core::Logger::Info("SaveSlots: switched from slot %d to slot %d (%s)", previous, index, created ? "new" : "loaded");
    NotifyChanged();
    return true;
}

bool ImportVanilla(int index, std::string& error) {
    if (!g_ready) {
        error = "save slots are not available";
        return false;
    }
    if (!ValidIndex(index) || g_slots[index].used) {
        error = "pick an empty slot";
        return false;
    }
    std::vector<std::uint8_t> plain;
    if (!ReadPlain(kVanillaSaveName, plain, error)) return false;
    if (plain.size() != Layout::kVanillaSize) {
        error = "cc_save.dat is not a vanilla save";
        return false;
    }
    Layout::Stash stash;
    std::vector<std::uint8_t> migrated;
    Layout::RemapReport report;
    const std::vector<std::string> current = CurrentKeys();
    if (!Layout::Remap(plain, {}, current, stash,
            [](std::size_t addon, std::uint8_t* character) {
                Characters::InitializeAddonCharacter(character, static_cast<int>(addon));
            },
            migrated, report, error)) {
        return false;
    }
    if (!WritePlain(SaveFileName(index), migrated)) {
        error = "cannot write " + SaveFileName(index);
        return false;
    }
    SlotState& slot = g_slots[index];
    slot.used = true;
    slot.meta = {};
    slot.meta.name = "Vanilla";
    slot.meta.created = Now();
    slot.meta.characters = current;
    slot.meta.layoutKnown = true;
    SaveMeta(index);
    Core::Logger::Info("SaveSlots: imported cc_save.dat into slot %d (+%u addon characters); cc_save.dat untouched",
                       index, static_cast<unsigned>(current.size()));
    if (!Activate(index, error)) {
        NotifyChanged();
        return false;
    }
    return true;
}

bool Delete(int index, std::string& error) {
    if (!g_ready || !ValidIndex(index)) {
        error = "invalid slot";
        return false;
    }
    if (index == g_active) {
        error = "the active slot cannot be deleted";
        return false;
    }
    const std::string trash = "hc_trash_slot" + std::to_string(index);
    const std::string files[][2] = {
        {SaveFileName(index), trash + ".dat"},
        {BackupFileName(index), trash + ".dat.bak"},
        {MetaFileName(index), trash + ".meta"},
    };
    for (const auto& file : files) {
        if (!g_storage.Exists(file[0])) continue;
        if (!CopyStorageFile(file[0], file[1])) {
            error = "cannot move " + file[0] + " to the trash";
            return false;
        }
    }
    for (const auto& file : files) {
        if (g_storage.Exists(file[0])) g_storage.Delete(file[0]);
    }
    g_storage.Delete(SaveFileName(index) + ".old");
    LoadSlot(index);
    Core::Logger::Info("SaveSlots: slot %d moved to %s.*", index, trash.c_str());
    NotifyChanged();
    return true;
}

void SetChangedCallback(void (*callback)()) {
    g_changed = callback;
}

} // namespace HookCrashers::Save::Slots
