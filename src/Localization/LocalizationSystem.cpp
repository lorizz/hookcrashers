#include "LocalizationSystem.h"

#include "LocalizationManager.h"
#include "../Characters/CharacterRegistry.h"
#include "../Core/Logger.h"
#include "../Core/Memory.h"
#include "../NativeDB/Classes.h"
#include "../NativeDB/Natives.h"

#include <windows.h>
#include <detours.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

namespace HookCrashers::Localization {
namespace {

constexpr std::uint16_t kGetLocalizationNativeId = 50200;
constexpr std::uintptr_t kRegisterNameRva = 0xFD530;
constexpr std::uintptr_t kRegisterAllRva = 0x1215B0;
constexpr std::uintptr_t kAddStringRva = 0xFD670;
constexpr std::uintptr_t kDispatcherRva = 0x11B2E0;
constexpr std::uintptr_t kTextObjectSetNTextRva = 0x125F50;
// Verified against native 0xC4 in the game's dispatcher (RVA 0x104750).
// 0xF0B60 is a branch operand inside a different function, not an entry point.
constexpr std::uintptr_t kIsCharacterUnlockedRva = 0xF0BD0;
constexpr std::uintptr_t kReadIntegerArgumentRva = 0x115E50;
constexpr std::uint16_t kIsCharacterUnlockedNativeId = 0xC4;

struct SWFArgument {
    std::uint32_t unknown0;
    std::uint32_t type;
    std::uint32_t unknown8;
    union { std::uint32_t raw; std::uint16_t stringId; } value;
};

struct SWFReturn {
    std::uint32_t status;
    std::uint32_t type;
    std::uint32_t padding;
    union { std::uint32_t raw; std::int32_t integer; } value;
};
struct SWFStringTable {
    std::uint32_t usedBytes;
    std::uint32_t arenaCapacity;
    std::uint8_t* arena;
    std::uint32_t inlineEmptyString;
    std::uint32_t reservedStringId;
    std::uint32_t nextStringId;
    std::uint32_t pointerCapacity;
    const char** strings;
    std::uint32_t hashCapacity;
    std::uint16_t* hashes;
};
static_assert(sizeof(SWFStringTable) == 0x28);

using RegisterNameFn = void(__thiscall*)(void* registry, std::uint16_t nativeId, const char* name);
using RegisterAllFn = void(__fastcall*)(void* context);
using DispatcherFn = void(__thiscall*)(void* thisPtr, int swfContext, std::uint32_t functionId,
                                      int paramCount, SWFArgument** args,
                                      std::uint32_t* result, std::uint32_t callbackPtr);
using AddStringFn = std::uint16_t(__thiscall*)(void* manager, std::uint32_t hashOrIndex, const char* value);
using SetNTextFn = std::uint16_t(__thiscall*)(void* textObject, std::int16_t stringId);

RegisterNameFn g_originalRegisterName = nullptr;
RegisterAllFn g_originalRegisterAll = nullptr;
DispatcherFn g_originalDispatcher = nullptr;
AddStringFn g_originalAddString = nullptr;
SetNTextFn g_originalSetNText = nullptr;
std::unordered_map<std::uint16_t, std::string> g_strings;
void* g_nativeRegistry = nullptr;
void* g_registeredRegistry = nullptr;
bool g_installed = false;
bool g_expandedUnlockSupported = false;

bool HasVerifiedUnlockNatives() {
    const auto* unlock = reinterpret_cast<const std::uint8_t*>(Core::Memory::Va(kIsCharacterUnlockedRva));
    const std::uint8_t unlockPrefix[] { 0x55, 0x8B, 0xEC, 0x8B, 0x0D };
    const std::uint8_t unlockSuffix[] { 0x83, 0xEC, 0x08, 0x57, 0xFF, 0x75, 0x08 };
    std::uint32_t profileGlobal = 0;
    std::memcpy(&profileGlobal, unlock + sizeof(unlockPrefix), sizeof(profileGlobal));
    const std::uint8_t numericPrefix[] {
        0x8B, 0x41, 0x04, 0x83, 0xC0, 0xFE, 0x83, 0xF8, 0x0E, 0x77, 0x4F, 0x0F, 0xB6
    };
    return std::memcmp(unlock, unlockPrefix, sizeof(unlockPrefix)) == 0 &&
           profileGlobal == Core::Memory::Va(0x391284) &&
           std::memcmp(unlock + 9, unlockSuffix, sizeof(unlockSuffix)) == 0 &&
           std::memcmp(reinterpret_cast<const void*>(Core::Memory::Va(kReadIntegerArgumentRva)),
                       numericPrefix, sizeof(numericPrefix)) == 0;
}

void __fastcall HookedRegisterName(void* registry, void*, std::uint16_t nativeId, const char* name) {
    if (nativeId == 0x14D && name && std::strcmp(name, "u_temp") == 0) {
        g_nativeRegistry = registry;
    }
    g_originalRegisterName(registry, nativeId, name);
}

void __fastcall HookedRegisterAll(void* context) {
    g_nativeRegistry = nullptr;
    g_originalRegisterAll(context);
    if (g_nativeRegistry && g_registeredRegistry != g_nativeRegistry) {
        g_originalRegisterName(g_nativeRegistry, kGetLocalizationNativeId, "GetLocalization");
        g_registeredRegistry = g_nativeRegistry;
        Core::Logger::Info("Localization: registered SWF native GetLocalization id=%u after RegisterAll", kGetLocalizationNativeId);
    }
}

std::uint16_t __fastcall HookedAddString(void* manager, void*, std::uint32_t hashOrIndex, const char* value) {
    auto* table = static_cast<SWFStringTable*>(manager);
    if (!table || !value) return 0;
    const std::size_t length = std::strlen(value) + 1;
    const bool pointerExhausted = table->nextStringId >= table->pointerCapacity;
    const bool arenaInvalid = table->usedBytes > table->arenaCapacity;
    const bool arenaExhausted = arenaInvalid || length > table->arenaCapacity - table->usedBytes;
    if (pointerExhausted || arenaExhausted) {
        Core::Logger::Error("Localization: SWF string table exhausted nextId=%u pointerCapacity=%u usedBytes=%u arenaCapacity=%u requestedBytes=%u",
                            table->nextStringId,
                            table->pointerCapacity,
                            table->usedBytes,
                            table->arenaCapacity,
                            static_cast<unsigned int>(length));
        return 0;
    }
    const std::uint16_t id = g_originalAddString(manager, hashOrIndex, value);
    if (id != 0 && value) g_strings[id] = value;
    return id;
}

std::uint16_t __fastcall HookedSetNText(void* textObject, void*, std::int16_t stringId) {
    const wchar_t* custom = LocalizationManager::Instance().StringForNumericId(static_cast<std::uint16_t>(stringId));
    if (!custom) return g_originalSetNText(textObject, stringId);

    auto* object = static_cast<std::uint8_t*>(textObject);
    *reinterpret_cast<std::uintptr_t*>(object + 0xF4) = 0;
    *reinterpret_cast<std::uintptr_t*>(object + 0xF8) = reinterpret_cast<std::uintptr_t>(custom);
    const std::size_t length = std::wcslen(custom);
    *reinterpret_cast<std::uint16_t*>(object + 0xC8) = static_cast<std::uint16_t>(length);
    return static_cast<std::uint16_t>(length);
}

void SetIntegerResult(std::uint32_t* rawResult, std::int32_t value) {
    auto* result = reinterpret_cast<SWFReturn*>(rawResult);
    if (!result) return;
    result->status = 0;
    result->type = 4;
    result->padding = 0;
    result->value.integer = value;
}

void SetBooleanResult(std::uint32_t* rawResult, bool value) {
    auto* result = reinterpret_cast<SWFReturn*>(rawResult);
    if (!result) return;
    result->status = 0;
    result->type = 2;
    result->padding = 0;
    result->value.raw = value ? 1u : 0u;
}

bool HandleExpandedCharacterUnlock(std::uint32_t functionId,
                                   int paramCount,
                                   SWFArgument** args,
                                   std::uint32_t* result) {
    if (!g_expandedUnlockSupported ||
        (functionId & 0xFFFFu) != kIsCharacterUnlockedNativeId ||
        paramCount < 2 || !args || !args[0] || !args[1]) {
        return false;
    }

    // Use the same numeric coercion as native 0xC4 (bool/int/string/float).
    // Reading value.raw directly treats float bits and SWF string IDs as integers.
    using ReadIntegerArgumentFn = std::uint32_t(__fastcall*)(SWFArgument*);
    const auto readInteger = reinterpret_cast<ReadIntegerArgumentFn>(Core::Memory::Va(kReadIntegerArgumentRva));
    const std::uint32_t playerIndex = readInteger(args[0]);
    const std::uint32_t characterId = readInteger(args[1]);
    if (playerIndex >= 4) {
        return false;
    }
    const auto& registry = Characters::Registry::Instance();
    const std::uint32_t firstAddonId =
        static_cast<std::uint32_t>(NativeDB::kVanillaBaseCharacterCount);
    const std::uint32_t firstWorkshopId =
        static_cast<std::uint32_t>(registry.NetworkBaseCount());

    if (characterId >= firstAddonId && characterId < firstWorkshopId) {
        using IsCharacterUnlockedFn = std::uint32_t(__cdecl*)(std::uint32_t, std::uint32_t);
        const auto isCharacterUnlocked = reinterpret_cast<IsCharacterUnlockedFn>(
            NativeDB::GetGameBase() + kIsCharacterUnlockedRva);
        SetBooleanResult(result, isCharacterUnlocked(playerIndex, characterId) != 0);
        return true;
    }

    return false;
}

void __fastcall HookedDispatcher(void* thisPtr, void*, int swfContext, std::uint32_t functionId,
                                 int paramCount, SWFArgument** args,
                                 std::uint32_t* result, std::uint32_t callbackPtr) {
    if (HandleExpandedCharacterUnlock(functionId, paramCount, args, result)) {
        return;
    }

    if ((functionId & 0xFFFFu) != kGetLocalizationNativeId) {
        g_originalDispatcher(thisPtr, swfContext, functionId, paramCount, args, result, callbackPtr);
        return;
    }

    int numericId = -1;
    if (paramCount >= 1 && args && args[0] && args[0]->type == 8) {
        const auto found = g_strings.find(args[0]->value.stringId);
        if (found != g_strings.end()) {
            numericId = LocalizationManager::Instance().NumericId(found->second);
        } else {
            Core::Logger::Warn("Localization: GetLocalization could not resolve SWF string id=%u", args[0]->value.stringId);
        }
    }
    SetIntegerResult(result, numericId);
}


bool AttachHook(PVOID* original, PVOID detour, const char* name) {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    const LONG attachResult = DetourAttach(original, detour);
    if (attachResult != NO_ERROR) {
        DetourTransactionAbort();
        Core::Logger::Error("Localization: DetourAttach failed hook=%s error=%ld", name, attachResult);
        return false;
    }
    const LONG commitResult = DetourTransactionCommit();
    if (commitResult != NO_ERROR) {
        Core::Logger::Error("Localization: DetourTransactionCommit failed hook=%s error=%ld", name, commitResult);
        return false;
    }
    Core::Logger::Info("Localization: hook installed name=%s", name);
    return true;
}

void DetachHook(PVOID* original, PVOID detour) {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(original, detour);
    DetourTransactionCommit();
}

} // namespace

bool Install() {
    if (g_installed) return true;
    g_expandedUnlockSupported = HasVerifiedUnlockNatives();
    if (!g_expandedUnlockSupported) {
        Core::Logger::Warn("Expanded addon unlock fallback disabled: native signatures do not match this executable");
    }
    g_originalRegisterName = reinterpret_cast<RegisterNameFn>(Core::Memory::Va(kRegisterNameRva));
    g_originalRegisterAll = reinterpret_cast<RegisterAllFn>(Core::Memory::Va(kRegisterAllRva));
    g_originalDispatcher = reinterpret_cast<DispatcherFn>(Core::Memory::Va(kDispatcherRva));
    g_originalAddString = reinterpret_cast<AddStringFn>(Core::Memory::Va(kAddStringRva));
    g_originalSetNText = reinterpret_cast<SetNTextFn>(Core::Memory::Va(kTextObjectSetNTextRva));
    if (!g_originalRegisterName || !g_originalRegisterAll || !g_originalDispatcher || !g_originalAddString || !g_originalSetNText) return false;

    // Match the v4 hook order. RegisterAll must be attached last.
    if (!AttachHook(&(PVOID&)g_originalRegisterName, reinterpret_cast<PVOID>(HookedRegisterName), "RegisterSWFFunction") ||
        !AttachHook(&(PVOID&)g_originalAddString, reinterpret_cast<PVOID>(HookedAddString), "AddString") ||
        !AttachHook(&(PVOID&)g_originalDispatcher, reinterpret_cast<PVOID>(HookedDispatcher), "CallSWFFunction") ||
        !AttachHook(&(PVOID&)g_originalSetNText, reinterpret_cast<PVOID>(HookedSetNText), "StringLookup") ||
        !AttachHook(&(PVOID&)g_originalRegisterAll, reinterpret_cast<PVOID>(HookedRegisterAll), "RegisterAllSWFFunctions")) {
        return false;
    }

    g_installed = true;
    Core::Logger::Info("Localization: custom dispatcher installed nativeId=%u baseId=%u",
                       kGetLocalizationNativeId, LocalizationManager::BaseId());
    return true;
}

void Remove() {
    if (!g_installed) return;
    DetachHook(&(PVOID&)g_originalRegisterAll, reinterpret_cast<PVOID>(HookedRegisterAll));
    DetachHook(&(PVOID&)g_originalSetNText, reinterpret_cast<PVOID>(HookedSetNText));
    DetachHook(&(PVOID&)g_originalDispatcher, reinterpret_cast<PVOID>(HookedDispatcher));
    DetachHook(&(PVOID&)g_originalAddString, reinterpret_cast<PVOID>(HookedAddString));
    DetachHook(&(PVOID&)g_originalRegisterName, reinterpret_cast<PVOID>(HookedRegisterName));
    g_installed = false;
    g_expandedUnlockSupported = false;
    g_nativeRegistry = nullptr;
    g_registeredRegistry = nullptr;
    g_strings.clear();
    LocalizationManager::Instance().Reset();
}

void Reset() { LocalizationManager::Instance().Reset(); }
bool LoadFile(const std::string& path, const std::string& sourceName) { return LocalizationManager::Instance().LoadFile(path, sourceName); }
bool Resolve(const std::string& logicalId, std::string& value) { return LocalizationManager::Instance().Resolve(logicalId, value); }

} // namespace HookCrashers::Localization
