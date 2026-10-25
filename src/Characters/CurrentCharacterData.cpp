#include "CurrentCharacterData.h"
#include "../Core/Memory.h"
#include "../Core/Logger.h"
#include "../NativeDB/Classes.h"
#include <windows.h>
#include <detours.h>
#include <tlhelp32.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// Stable undecorated exports for consumers using GetProcAddress on Win32.
#pragma comment(linker, "/EXPORT:HookCrashers_GetCurrentCharacterData=_HookCrashers_GetCurrentCharacterData@12")
#pragma comment(linker, "/EXPORT:GetCurrentCharacterData=_HookCrashers_GetCurrentCharacterData@12")
#pragma comment(linker, "/EXPORT:HookCrashers_GetCurrentCharacterProperty=_HookCrashers_GetCurrentCharacterProperty@24")
#pragma comment(linker, "/EXPORT:HookCrashers_RegisterUpdateCallback=_HookCrashers_RegisterUpdateCallback@4")
#pragma comment(linker, "/EXPORT:HookCrashers_UnregisterUpdateCallback=_HookCrashers_UnregisterUpdateCallback@4")

namespace HookCrashers::Characters::CurrentData {
namespace {
constexpr unsigned kMaximumProperties = 512;
constexpr unsigned kMaximumDepth = 3;
constexpr unsigned kMaximumObjects = 64;
constexpr std::uint32_t kNoParent = UINT32_MAX;
constexpr std::uintptr_t kFrameUpdateRva = 0x1082D0;
constexpr std::uintptr_t kProfileGlobalRva = 0x391294;
using Argument = NativeDB::CC_AVMArgument;
using StoredValue = NativeDB::CC_AVMPropertyValue;
using Entry = NativeDB::CC_AVMPropertyEntry;
using Map = NativeDB::CC_AVMPropertyMap;
static_assert(sizeof(void*) == 4, "Current character reader requires x86");
static_assert(sizeof(Argument) == 16 && sizeof(StoredValue) == 12 && sizeof(Entry) == 8);
struct Snapshot {
    CurrentCharacterData data {};
    std::array<CurrentCharacterProperty, kMaximumProperties> properties {};
};
Snapshot g_Cache[4], g_Work[4];
SRWLOCK g_Lock = SRWLOCK_INIT;
volatile LONG g_Capturing = 0;
std::uint64_t g_Sequence = 0;
bool g_Installed = false;
void* g_OriginalFrameUpdate = nullptr;
struct UpdateSlot { UpdateCallback callback = nullptr; volatile LONG faulted = 0; };
UpdateSlot g_UpdateSlots[16];
SRWLOCK g_UpdateLock = SRWLOCK_INIT;
__declspec(thread) bool g_InUpdateCallback = false;
void InvokeUpdate(UpdateSlot& slot) {
    __try { slot.callback(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&slot.faulted, 1);
        Core::Logger::Error("Update callback faulted; disabled until unregistered");
    }
}
void DispatchUpdates() {
    // A successful unregister waits for in-flight calls before the DLL can unload.
    AcquireSRWLockShared(&g_UpdateLock);
    g_InUpdateCallback = true;
    for (auto& slot : g_UpdateSlots)
        if (slot.callback && !slot.faulted) InvokeUpdate(slot);
    g_InUpdateCallback = false;
    ReleaseSRWLockShared(&g_UpdateLock);
}

template<class T> bool Read(std::uintptr_t address, T& output) {
    if (address < 0x10000 || address > UINT32_MAX - sizeof(T)) return false;
    __try { std::memcpy(&output, reinterpret_cast<const void*>(address), sizeof(T)); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool CopyOut(void* output, const void* data, std::size_t size) {
    __try { std::memcpy(output, data, size); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::uintptr_t Pointer(std::uintptr_t address) {
    std::uint32_t value = 0;
    Read(address, value);
    return value;
}
bool EqualString(std::uintptr_t address, const char* expected) {
    __try {
        if (address < 0x10000) return false;
        for (unsigned i = 0; i < 128; ++i) {
            const char c = reinterpret_cast<const char*>(address)[i];
            if (c != expected[i]) return false;
            if (!c) return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return false;
}
void CopyString(std::uintptr_t address, char* output, unsigned capacity, std::uint32_t& flags) {
    if (!address) { flags |= CharacterProperty_ReadFault; return; }
    __try {
        unsigned i = 0;
        for (; i + 1 < capacity; ++i) {
            output[i] = reinterpret_cast<const char*>(address)[i];
            if (!output[i]) return;
        }
        if (reinterpret_cast<const char*>(address)[i]) flags |= CharacterProperty_Truncated;
        output[i] = 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        output[capacity - 1] = 0;
        flags |= CharacterProperty_ReadFault;
    }
}
// Reproduce the native hash/lookup without modifying its +28/+2C cursors or
// interning names: this reader must not grow or alter the game's string table.
std::uint16_t NameId(std::uintptr_t strings, const char* name) {
    std::uint32_t capacity = 0;
    if (!Read(strings + 0x20, capacity) || !capacity || capacity > 0x10000 ||
        (capacity & (capacity - 1))) return 0;
    const auto hashes = Pointer(strings + 0x24);
    const auto pointers = Pointer(strings + 0x1C);
    if (!hashes || !pointers) return 0;
    std::uint32_t hash = 0, weight = 0x77;
    for (const auto* p = reinterpret_cast<const unsigned char*>(name); *p; ++p)
        hash += *p * weight++;
    hash &= capacity - 1;
    for (unsigned i = 0; i < capacity; ++i) {
        std::uint16_t id = 0;
        if (!Read(hashes + hash * 2, id) || !id) return 0;
        if (EqualString(Pointer(pointers + id * 4), name)) return id;
        hash = (hash + 1) & (capacity - 1);
    }
    return 0;
}
std::uintptr_t StringAddress(std::uintptr_t strings, std::uint16_t id) {
    if (!id) return strings + 0xC;
    std::uint32_t capacity = 0;
    if (!Read(strings + 0x18, capacity) || capacity > 0x10000 || id >= capacity) return 0;
    return Pointer(Pointer(strings + 0x1C) + id * 4);
}
bool Member(std::uintptr_t object, std::uint16_t name, Argument& result) {
    result = {0, 0x80000000, 0, 0};
    if (!object || !name) return false;
    const auto function = Pointer(Pointer(object) + 0xC);
    if (!function) return false;
    using Getter = std::uint8_t(__thiscall*)(void*, std::uint32_t, Argument*);
    __try { return reinterpret_cast<Getter>(function)(reinterpret_cast<void*>(object), name, &result) != 0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Member(std::uintptr_t object, std::uintptr_t strings, const char* name, Argument& result) {
    return Member(object, NameId(strings, name), result);
}
std::uintptr_t Object(const Argument& argument) {
    if ((argument.type & 0x7C6C1) || argument.type == 0x1000 || argument.type == 0x2000)
        return argument.object;
    return 0;
}
std::uintptr_t Child(std::uintptr_t object, std::uintptr_t strings, const char* name) {
    Argument argument {};
    return Member(object, strings, name, argument) ? Object(argument) : 0;
}
bool Numeric(const Argument& value, double& number) {
    switch (value.type) {
    case 2: number = static_cast<std::uint8_t>(value.scalar); break;
    case 4: number = static_cast<std::int32_t>(value.scalar); break;
    case 16: { float f = 0; std::memcpy(&f, &value.scalar, 4); number = f; break; }
    default: return false;
    }
    return std::isfinite(number);
}
struct Binding { const char* name; std::size_t offset; };
#define BIND(name, member) {name, offsetof(CurrentCharacterData, member)}
constexpr Binding kBindings[] {
    BIND("p_type", characterId), BIND("port", savePort), BIND("alive", alive),
    BIND("health", health), BIND("health_max", healthMax), BIND("magic_current", magicCurrent),
    BIND("magic_max", magicMax), BIND("level", level), BIND("exp", experience),
    BIND("exp_next", experienceNext), BIND("gold", gold), BIND("strength", strength),
    BIND("defense", defense), BIND("magic", magic), BIND("agility", agility),
    BIND("weapon", weapon), BIND("weapon_repaint", weaponRepaint), BIND("animal_type", animalType),
    BIND("healthpots", healthPotions), BIND("bombs", bombs), BIND("beefies", beefies),
    BIND("equippeditem", equippedItem), BIND("kills", kills), BIND("resists", resists),
    BIND("xpgained", unspentStatPoints), BIND("insane_mode", insaneMode)
};
#undef BIND
static_assert(std::size(kBindings) == static_cast<unsigned>(CurrentCharacterField::Count));

struct Walker {
    Snapshot& snapshot;
    std::uintptr_t strings;
    std::array<std::uintptr_t, kMaximumObjects> seen {};
    unsigned seenCount = 0;
    void ObjectProperties(std::uintptr_t object, std::uint32_t parent, unsigned depth);
    void Value(const Argument& value, std::uint32_t index, unsigned depth) {
        auto& property = snapshot.properties[index];
        property.nativeType = value.type;
        switch (value.type) {
        case 0x80000000: property.kind = CharacterPropertyKind::Undefined; return;
        case 2: property.kind = CharacterPropertyKind::Boolean; property.number = value.scalar & 0xFF; return;
        case 4: property.kind = CharacterPropertyKind::Integer; property.number = static_cast<std::int32_t>(value.scalar); return;
        case 16: {
            float number; std::memcpy(&number, &value.scalar, 4);
            property.kind = CharacterPropertyKind::Float; property.number = number; return;
        }
        case 8:
            property.kind = CharacterPropertyKind::String;
            CopyString(StringAddress(strings, static_cast<std::uint16_t>(value.scalar)), property.text,
                       sizeof(property.text), property.flags);
            return;
        }
        const auto object = Object(value);
        if (!object) { property.kind = CharacterPropertyKind::Opaque; return; }
        property.kind = CharacterPropertyKind::Object;
        std::uint32_t actualType = 0;
        if (!Read(object + 0x18, actualType)) { property.flags |= CharacterProperty_ReadFault; return; }
        if (actualType == 0x80) property.kind = CharacterPropertyKind::Array;
        for (unsigned i = 0; i < seenCount; ++i) if (seen[i] == object) {
            property.referenceId = i + 1;
            property.flags |= CharacterProperty_Reference;
            return;
        }
        if (seenCount == seen.size() || depth >= kMaximumDepth) {
            property.flags |= CharacterProperty_DepthLimit;
            snapshot.data.flags |= CharacterData_Truncated;
            return;
        }
        seen[seenCount++] = object;
        property.referenceId = seenCount;
        if (actualType == 0x80) {
            Argument length {};
            double count = 0;
            if (!Member(object, 1, length) || !Numeric(length, count) || count < 0 || count > 65536 ||
                count != std::floor(count)) { property.flags |= CharacterProperty_ReadFault; return; }
            property.number = count;
            const auto elements = Pointer(object + 0x20); // native array getter uses 16-byte slots
            for (unsigned i = 0; i < static_cast<unsigned>(count); ++i) {
                const auto child = Add(index);
                if (child == kNoParent) break;
                std::snprintf(snapshot.properties[child].name, sizeof(property.name), "%u", i);
                Argument element {};
                if (!Read(elements + i * sizeof(Argument), element)) {
                    snapshot.properties[child].flags |= CharacterProperty_ReadFault;
                    break;
                }
                Value(element, child, depth + 1);
            }
        } else if (actualType == 1) {
            ObjectProperties(object, index, depth + 1);
        } else {
            // Movie clips, functions and engine objects can lead to the entire
            // scene graph. Keep an opaque reference rather than walking them.
            property.flags |= CharacterProperty_Reference;
        }
    }
    std::uint32_t Add(std::uint32_t parent) {
        if (snapshot.data.propertyCount == kMaximumProperties) {
            snapshot.data.flags |= CharacterData_Truncated;
            return kNoParent;
        }
        const auto index = snapshot.data.propertyCount++;
        auto& property = snapshot.properties[index];
        property = {};
        property.structSize = sizeof(property);
        property.schemaVersion = 1;
        property.sequence = snapshot.data.sequence;
        property.parentIndex = parent;
        return index;
    }
};
void Walker::ObjectProperties(std::uintptr_t object, std::uint32_t parent, unsigned depth) {
    Map map {};
    if (!Read(object + 4, map) || map.count > 8192 || (map.count && !map.entries)) {
        snapshot.data.flags |= CharacterData_ReadFault; return;
    }
    for (unsigned i = 0; i < map.count; ++i) {
        Entry entry {}; StoredValue stored {};
        if (!Read(map.entries + i * sizeof(Entry), entry) || !Read(entry.value, stored)) {
            snapshot.data.flags |= CharacterData_ReadFault; break;
        }
        const auto index = Add(parent);
        if (index == kNoParent) break;
        auto& property = snapshot.properties[index];
        CopyString(StringAddress(strings, entry.name), property.name, sizeof(property.name), property.flags);
        Value({stored.object, stored.type, 0, stored.scalar}, index, depth);
        if (property.flags & CharacterProperty_Truncated) snapshot.data.flags |= CharacterData_Truncated;
        if (property.flags & CharacterProperty_ReadFault) snapshot.data.flags |= CharacterData_ReadFault;
    }
}

void Build(std::uintptr_t context) {
    const auto sequence = ++g_Sequence;
    const auto now = GetTickCount64();
    const auto root = context ? Pointer(context + 0x5C) : 0; // native _root (0x68)
    const auto strings = context ? Pointer(context + 0x2F2C) : 0;
    Argument loadingArgument {};
    double loading = 0;
    const bool isLoading = root && Member(root, strings, "g_bLoading", loadingArgument) &&
                           Numeric(loadingArgument, loading) && loading != 0;
    for (unsigned slot = 1; slot <= 4; ++slot) {
        auto& snapshot = g_Work[slot - 1];
        snapshot.data = {};
        auto& data = snapshot.data;
        data.structSize = sizeof(data); data.schemaVersion = 1; data.sequence = sequence;
        data.capturedAtMilliseconds = now; data.playerSlot = slot;
        data.result = CharacterDataResult::Unavailable;
        data.flags = CharacterData_FrameBoundary;
        if (!root || !strings) continue;
        char name[8]; std::snprintf(name, sizeof(name), "hud%u", slot);
        const auto hud = Child(root, strings, name);
        if (!hud) continue;
        auto player = isLoading ? 0 : Child(hud, strings, "player_pt");
        // HUD links are accepted only when the live object's reciprocal HUD
        // reference matches. Otherwise resolve the explicit game p1..p4 path.
        if (player && Child(player, strings, "hud_pt") != hud) player = 0;
        if (!player && !isLoading) {
            const auto loader = Child(root, strings, "loader");
            const auto game = Child(Child(loader, strings, "game"), strings, "game");
            std::snprintf(name, sizeof(name), "p%u", slot);
            player = Child(game, strings, name);
            const auto owner = player ? Child(player, strings, "hud_pt") : 0;
            if (owner && owner != hud) player = 0;
        }
        const auto source = player ? player : hud;
        data.result = CharacterDataResult::Ok;
        data.source = player ? CharacterDataSource::LivePlayer : CharacterDataSource::Hud;
        data.state = isLoading ? CharacterDataState::Loading :
                     player ? CharacterDataState::InLevel : CharacterDataState::HudOnly;
        for (unsigned i = 0; i < std::size(kBindings); ++i) {
            Argument value {}; double number = 0;
            if (Member(source, strings, kBindings[i].name, value) && Numeric(value, number)) {
                *reinterpret_cast<double*>(reinterpret_cast<char*>(&data) + kBindings[i].offset) = number;
                data.validFields |= std::uint64_t{1} << i;
            }
        }
        // Port belongs to the HUD even when live player data was selected.
        Argument port {}; double portNumber = 0;
        if (Member(hud, strings, "port", port) && Numeric(port, portNumber) &&
            portNumber >= 1 && portNumber <= 4 && portNumber == std::floor(portNumber)) {
            data.savePort = portNumber - 1; // Match ReadStorage's zero-based port.
            data.validFields |= std::uint64_t{1} << static_cast<unsigned>(CurrentCharacterField::SavePort);
        } else data.validFields &= ~(std::uint64_t{1} << static_cast<unsigned>(CurrentCharacterField::SavePort));
        // In-level pets can keep their type on the attached pet clip rather
        // than directly on the player (the transition script copies it to HUD).
        if (player && !HasCharacterField(data, CurrentCharacterField::AnimalType)) {
            const auto pet = Child(player, strings, "pet");
            Argument type {}; double number = 0;
            if (pet && Member(pet, strings, "animal_type", type) && Numeric(type, number)) {
                data.animalType = number;
                data.validFields |= std::uint64_t{1} << static_cast<unsigned>(CurrentCharacterField::AnimalType);
            }
        }
        if (!player && HasCharacterField(data, CurrentCharacterField::Alive) && !data.alive && !isLoading)
            data.state = CharacterDataState::Inactive;
        Walker walker {snapshot, strings};
        walker.seen[walker.seenCount++] = source;
        walker.ObjectProperties(source, kNoParent, 0);
        for (unsigned i = 0; i < data.propertyCount; ++i) {
            if (snapshot.properties[i].flags & CharacterProperty_Truncated) data.flags |= CharacterData_Truncated;
            if (snapshot.properties[i].flags & CharacterProperty_ReadFault) data.flags |= CharacterData_ReadFault;
        }
    }
    std::array<bool, 4> changed {};
    AcquireSRWLockExclusive(&g_Lock);
    for (unsigned i = 0; i < 4; ++i) {
        changed[i] = !g_Cache[i].data.sequence || g_Cache[i].data.source != g_Work[i].data.source ||
                     g_Cache[i].data.state != g_Work[i].data.state;
        g_Cache[i].data = g_Work[i].data;
        std::memcpy(g_Cache[i].properties.data(), g_Work[i].properties.data(),
                    sizeof(CurrentCharacterProperty) * g_Work[i].data.propertyCount);
    }
    ReleaseSRWLockExclusive(&g_Lock);
    for (unsigned i = 0; i < 4; ++i) if (changed[i]) {
        const auto& data = g_Work[i].data;
        Core::Logger::Info("CurrentCharacterData: slot=%u source=%u state=%u properties=%u flags=0x%X valid=0x%llX",
                           i + 1, static_cast<unsigned>(data.source), static_cast<unsigned>(data.state),
                           data.propertyCount, data.flags, static_cast<unsigned long long>(data.validFields));
    }
}
void CaptureProfile() {
    const auto profile = Pointer(Core::Memory::Va(kProfileGlobalRva));
    const auto movie = profile ? Pointer(profile + 0x190) : 0;
    const auto context = movie ? Pointer(movie + 0x70) : 0;
    CaptureFromContext(reinterpret_cast<void*>(context));
}
// RVA 0x1082D0 consumes ECX plus a private XMM1 time-step argument, with RET 0.
// Call it before running any C++ code; a regular wrapper can clobber XMM1.
// Preserve the complete x87/SSE state around our post-update callback.
__declspec(naked) void FrameHook() {
    __asm {
        call dword ptr [g_OriginalFrameUpdate]
        pushfd
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        call CaptureProfile
        fxrstor [esp]
        mov esp, ebx
        popad
        popfd
        ret
    }
}
}

void CaptureFromContext(void* context) {
    if (InterlockedCompareExchange(&g_Capturing, 1, 0) != 0) return;
    // Catch invalid native memory without allowing an SEH exception to unwind
    // through the assembly hook. No owning C++ objects live in this function.
    __try { Build(reinterpret_cast<std::uintptr_t>(context)); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        AcquireSRWLockExclusive(&g_Lock);
        for (auto& snapshot : g_Cache) {
            snapshot.data = {};
            snapshot.data.structSize = sizeof(CurrentCharacterData);
            snapshot.data.schemaVersion = 1;
            snapshot.data.result = CharacterDataResult::Unavailable;
            snapshot.data.flags = CharacterData_ReadFault;
            snapshot.data.sequence = ++g_Sequence;
        }
        ReleaseSRWLockExclusive(&g_Lock);
    }
    DispatchUpdates();
    InterlockedExchange(&g_Capturing, 0);
}

bool Install() {
    if (g_Installed) return true;
    // Expected prologue intentionally excludes the relocated security-cookie operand.
    const std::uint8_t prefix[] {0x55,0x8B,0xEC,0x81,0xEC,0x24,0x01,0x00,0x00,0xA1};
    const std::uint8_t suffix[] {0x33,0xC5,0x89,0x45,0xFC,0x53,0x56,0x57,0x8B,0xF9,0x0F,0x28,0xC1};
    const auto* target = reinterpret_cast<const std::uint8_t*>(Core::Memory::Va(kFrameUpdateRva));
    if (std::memcmp(target, prefix, sizeof(prefix)) != 0 ||
        std::memcmp(target + 0xE, suffix, sizeof(suffix)) != 0) {
        Core::Logger::Error("CurrentCharacterData: unsupported frame update entry"); return false;
    }
    g_OriginalFrameUpdate = const_cast<std::uint8_t*>(target);
    if (DetourTransactionBegin() != NO_ERROR) return false;
    std::vector<HANDLE> threads;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    bool ok = snapshot != INVALID_HANDLE_VALUE;
    THREADENTRY32 entry {sizeof(entry)};
    if (ok) {
        ok = Thread32First(snapshot, &entry) != 0;
        if (ok) do {
            if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId()) continue;
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                                       FALSE, entry.th32ThreadID);
            if (!thread) { ok = false; break; }
            threads.push_back(thread);
        } while (Thread32Next(snapshot, &entry));
        CloseHandle(snapshot);
    }
    if (ok) ok = DetourAttach(&g_OriginalFrameUpdate, reinterpret_cast<PVOID>(FrameHook)) == NO_ERROR;
    if (ok) for (HANDLE thread : threads) if (DetourUpdateThread(thread) != NO_ERROR) { ok = false; break; }
    const auto result = ok ? DetourTransactionCommit() : DetourTransactionAbort();
    for (HANDLE thread : threads) CloseHandle(thread);
    g_Installed = ok && result == NO_ERROR;
    Core::Logger::Info("CurrentCharacterData capture hook install=%d", g_Installed ? 1 : 0);
    return g_Installed;
}
void Shutdown() {
    if (!g_Installed) return;
    if (DetourTransactionBegin() != NO_ERROR) return;
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(&g_OriginalFrameUpdate, reinterpret_cast<PVOID>(FrameHook));
    if (DetourTransactionCommit() == NO_ERROR) g_Installed = false;
}
}

extern "C" __declspec(dllexport) bool __stdcall HookCrashers_RegisterUpdateCallback(
    HookCrashers::UpdateCallback callback) {
    using namespace HookCrashers::Characters::CurrentData;
    if (!callback || g_InUpdateCallback) return false;
    AcquireSRWLockExclusive(&g_UpdateLock);
    UpdateSlot* available = nullptr;
    bool found = false;
    for (auto& slot : g_UpdateSlots) {
        if (slot.callback == callback) found = true;
        if (!slot.callback && !available) available = &slot;
    }
    const bool ok = !found && available;
    if (ok) { available->faulted = 0; available->callback = callback; }
    ReleaseSRWLockExclusive(&g_UpdateLock);
    return ok;
}
extern "C" __declspec(dllexport) bool __stdcall HookCrashers_UnregisterUpdateCallback(
    HookCrashers::UpdateCallback callback) {
    using namespace HookCrashers::Characters::CurrentData;
    if (!callback || g_InUpdateCallback) return false;
    AcquireSRWLockExclusive(&g_UpdateLock);
    bool found = false;
    for (auto& slot : g_UpdateSlots) if (slot.callback == callback) {
        slot.callback = nullptr; slot.faulted = 0; found = true;
    }
    ReleaseSRWLockExclusive(&g_UpdateLock);
    return found;
}

extern "C" __declspec(dllexport)
HookCrashers::CharacterDataResult __stdcall HookCrashers_GetCurrentCharacterData(
    std::uint32_t player, HookCrashers::CurrentCharacterData* output, std::uint32_t size) {
    using namespace HookCrashers;
    using namespace HookCrashers::Characters::CurrentData;
    if (!output || size < sizeof(CurrentCharacterData)) return CharacterDataResult::InvalidBuffer;
    if (player < 1 || player > 4) return CharacterDataResult::InvalidPlayer;
    AcquireSRWLockShared(&g_Lock);
    auto data = g_Cache[player - 1].data;
    ReleaseSRWLockShared(&g_Lock);
    data.structSize = sizeof(data); data.schemaVersion = 1; data.playerSlot = player;
    if (!data.sequence || data.source == CharacterDataSource::None) data.result = CharacterDataResult::Unavailable;
    else if (GetTickCount64() - data.capturedAtMilliseconds > 500) data.result = CharacterDataResult::Stale;
    return CopyOut(output, &data, sizeof(data)) ? data.result : CharacterDataResult::InvalidBuffer;
}
extern "C" __declspec(dllexport)
HookCrashers::CharacterDataResult __stdcall HookCrashers_GetCurrentCharacterProperty(
    std::uint32_t player, std::uint64_t sequence, std::uint32_t index,
    HookCrashers::CurrentCharacterProperty* output, std::uint32_t size) {
    using namespace HookCrashers;
    using namespace HookCrashers::Characters::CurrentData;
    if (!output || size < sizeof(CurrentCharacterProperty)) return CharacterDataResult::InvalidBuffer;
    if (player < 1 || player > 4) return CharacterDataResult::InvalidPlayer;
    AcquireSRWLockShared(&g_Lock);
    const auto& snapshot = g_Cache[player - 1];
    CharacterDataResult result = CharacterDataResult::Ok;
    CurrentCharacterProperty property {};
    if (!snapshot.data.sequence) result = CharacterDataResult::Unavailable;
    else if (snapshot.data.sequence != sequence) result = CharacterDataResult::SequenceChanged;
    else if (GetTickCount64() - snapshot.data.capturedAtMilliseconds > 500) result = CharacterDataResult::Stale;
    else if (index >= snapshot.data.propertyCount) result = CharacterDataResult::InvalidProperty;
    else property = snapshot.properties[index];
    ReleaseSRWLockShared(&g_Lock);
    if (result != CharacterDataResult::Ok) return result;
    return CopyOut(output, &property, sizeof(property)) ? result : CharacterDataResult::InvalidBuffer;
}
