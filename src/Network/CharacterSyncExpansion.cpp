#include "CharacterSyncExpansion.h"
#include "LobbyGameMode.h"

#include "../Characters/CharacterRegistry.h"
#include "../Core/Logger.h"
#include "../Core/Memory.h"
#include "../NativeDB/Classes.h"
#include "../NativeDB/Natives.h"
#include <windows.h>
#include <detours.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace HookCrashers::Network {
namespace {

constexpr std::uint32_t kVanillaBaseEntriesWithDummy = 33;
constexpr std::uint32_t kWorkshopEntriesPerPlayer = 10;
constexpr std::uint32_t kMaxLocalPlayers = 4;
constexpr std::uint32_t kWorkshopEntries = kWorkshopEntriesPerPlayer * kMaxLocalPlayers;
constexpr std::uint32_t kCharacterIdBytes = 8;
constexpr std::uintptr_t kLobbyManagerGlobalRva = 0x3912B0;
constexpr std::uintptr_t kCharacterRuntimeBaseArrayOffset = 0x11AC;
constexpr std::uintptr_t kNetworkKickReasonGlobalRva = 0x260E94;
constexpr std::uint8_t kCharacterSyncMessageType = 0x0D;
// v6 transmits the actual mode; v5 always advertised Fresh and cannot be trusted.
constexpr std::uint16_t kHookCrashersSyncVersion = 6;
constexpr std::uintptr_t kSetFreshModeRva = 0x10A540;
constexpr std::array<std::uint8_t, 4> kHookCrashersSyncMagic { 'H', 'C', '5', 'C' };
constexpr std::uint32_t kAddonSkinIdHi = 0x48433541; // HC5A
// CALL at RVA 0x87A3D in the vanilla roster receiver targets this map insert.
// 0x12B0E0 lies inside an unrelated instruction, not a callable entry point.
constexpr std::uintptr_t kInsertCharacterSyncEntryRva = 0x2B030;


using ResolveCharacterSkinRuntimeFn = void*(__thiscall*)(void* lobbyManager, void* characterRuntimeKey);
using SendInitialCharacterSyncPacketFn = void(__thiscall*)(void* self, std::uint32_t* outResult, void* peer);
using HandleIncomingCharacterSyncPacketFn = void(__thiscall*)(void* self, std::uint32_t* outResult, void* transport, std::uint8_t* payload);
using ChannelForLocalPlayerFn = void*(__thiscall*)(void* session, std::uint32_t localPlayer);
using ChannelSendFn = void(__thiscall*)(void* channel,
                                        std::uint32_t* outScratch,
                                        void* peer,
                                        const void* message,
                                        std::uint32_t byteCount,
                                        std::uint32_t reliable);
using PeerBoolFn = std::uint8_t(__thiscall*)(void* peer);
using SetFreshModeFn = void(__stdcall*)(std::uint8_t fresh);
using InsertCharacterSyncEntryFn = void*(__thiscall*)(void* map,
                                                      void* outPair,
                                                      void* keyForSearch,
                                                      void* keyForInsert,
                                                      void* packet);

SendInitialCharacterSyncPacketFn g_OriginalSendInitialCharacterSyncPacket = nullptr;
HandleIncomingCharacterSyncPacketFn g_OriginalHandleIncomingCharacterSyncPacket = nullptr;
SetFreshModeFn g_SetFreshMode = nullptr;

void __fastcall DetouredHandleIncomingCharacterSyncPacket(void* self,
                                                          void*,
                                                          std::uint32_t* outResult,
                                                          void* transport,
                                                          std::uint8_t* payload);

struct ExpandedPacket {
    CharacterSyncPacketLayout layout {};
    LobbyGameMode gameMode = LobbyGameMode::Unknown;
    std::vector<std::uint8_t> bytes;
};

// Verified in the original sender at RVA F216F..F21AA and setter 10A540:
// profile global 391294 -> map +814 -> key 0120 -> node value +14.
// Walk the native std::map without operator[]: reading must not create a setting.
bool ReadLocalGameMode(LobbyGameMode& mode) {
    mode = LobbyGameMode::Unknown;
    __try {
        const auto profile = *reinterpret_cast<const std::uintptr_t*>(Core::Memory::Va(0x391294));
        if (!profile) return false;
        const auto head = *reinterpret_cast<const std::uintptr_t*>(profile + 0x814);
        if (!head) return false;
        auto node = *reinterpret_cast<const std::uintptr_t*>(head + 4);
        for (unsigned depth = 0; node && node != head && depth < 128; ++depth) {
            if (*reinterpret_cast<const std::uint8_t*>(node + 0xD)) return false;
            const auto key = *reinterpret_cast<const std::uint16_t*>(node + 0x10);
            if (key == 0x120) {
                const auto value = *reinterpret_cast<const std::uint32_t*>(node + 0x14);
                if (value > 1) return false;
                mode = static_cast<LobbyGameMode>(value);
                return true;
            }
            node = *reinterpret_cast<const std::uintptr_t*>(node + (key < 0x120 ? 8 : 0));
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return false;
}

void RejectCharacterSync(void* self,std::uint32_t* result,const char* reason,
                         LobbyGameMode local,LobbyGameMode remote) {
    Core::Logger::Error("Lobby rejected: %s; localMode=%s remoteMode=%s (no roster applied)",
                        reason,LobbyGameModeName(local),LobbyGameModeName(remote));
    // Use the existing native incompatibility path, not an invented error code.
    *reinterpret_cast<std::uint32_t*>(Core::Memory::Va(kNetworkKickReasonGlobalRva)) = 0x0D;
    if (self) *reinterpret_cast<std::uint8_t*>(static_cast<std::uint8_t*>(self) + 0x29A5) = 0;
    if (result) { result[0] = 1; result[1] = 0; }
}

struct CharacterNetId {
    std::uint32_t lo = 0;
    std::uint32_t hi = 0;
};

struct InsertResult {
    void* node = nullptr;
    std::uint8_t inserted = 0;
    std::uint8_t pad[3] {};
};

template <typename T>
T ReadUnaligned(const std::uint8_t* data) {
    T value {};
    std::memcpy(&value, data, sizeof(T));
    return value;
}

template <typename T>
void AppendUnaligned(std::vector<std::uint8_t>& out, T value) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

void SetResult(std::uint32_t* outResult, std::uint32_t value) {
    if (!outResult) {
        return;
    }
    outResult[0] = value;
    outResult[1] = 0;
}

void* GetVfunc(void* object, std::uintptr_t offset) {
    if (!object) {
        return nullptr;
    }
    return *reinterpret_cast<void**>(*reinterpret_cast<std::uintptr_t*>(object) + offset);
}

bool CallPeerBool(void* peer, std::uintptr_t vtableOffset) {
    auto fn = reinterpret_cast<PeerBoolFn>(GetVfunc(peer, vtableOffset));
    return fn ? fn(peer) != 0 : false;
}

bool ApplyAuthoritativeGameMode(void* self,void* peer,LobbyGameMode remoteMode) {
    if (!self || !IsKnownLobbyMode(remoteMode)) return false;
    const auto* state = static_cast<const std::uint8_t*>(self);
    const bool syncEnabled = state[0x29A4] != 0;
    if (!syncEnabled || !CallPeerBool(peer,0x20)) return true;
    void* session = *reinterpret_cast<void* const*>(state+0x0C);
    if (!ShouldAdoptHostMode(syncEnabled,true,session && CallPeerBool(session,0x68))) return true;
    if (!g_SetFreshMode) return false;
    LobbyGameMode before;
    ReadLocalGameMode(before);
    // Vanilla does this before roster building. A client follows its host;
    // receiving a client's packet must never switch the host's own setting.
    g_SetFreshMode(remoteMode == LobbyGameMode::Fresh ? 1 : 0);
    LobbyGameMode after;
    if (!ReadLocalGameMode(after) || after != remoteMode) {
        Core::Logger::Error("Failed to apply authoritative lobby mode %s",LobbyGameModeName(remoteMode));
        return false;
    }
    if (before != after) Core::Logger::Info("Lobby mode follows host: %s -> %s",LobbyGameModeName(before),LobbyGameModeName(after));
    return true;
}

void WriteCharacterSkinId(void* destination, void* skinRuntime) {
    auto* bytes = static_cast<std::uint8_t*>(destination);
    std::uint32_t lo = 0;
    std::uint32_t hi = 0;

    if (skinRuntime) {
        const auto explicitId = *reinterpret_cast<std::int32_t*>(static_cast<std::uint8_t*>(skinRuntime) + 0x1C);
        const auto idLo = *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(skinRuntime) + 0x20);
        const auto idHi = *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(skinRuntime) + 0x24);
        if (explicitId != -1) {
            lo = static_cast<std::uint32_t>(explicitId);
            hi = 0xBE400000;
        } else if (idLo != 0 || idHi != 0) {
            lo = idLo;
            hi = idHi;
        }
    }

    std::memcpy(bytes, &lo, sizeof(lo));
    std::memcpy(bytes + sizeof(lo), &hi, sizeof(hi));
}

CharacterNetId ReadCharacterSkinId(const std::uint8_t* packet, std::uint32_t offset, int index) {
    const auto* entry = packet + offset + index * kCharacterIdBytes;
    return {
        ReadUnaligned<std::uint32_t>(entry),
        ReadUnaligned<std::uint32_t>(entry + sizeof(std::uint32_t))
    };
}

CharacterNetId RuntimeNodeSkinId(void* node) {
    if (!node) {
        return {};
    }

    const auto* bytes = static_cast<std::uint8_t*>(node);
    const auto explicitId = *reinterpret_cast<const std::int32_t*>(bytes + 0x2C);
    const auto lo = *reinterpret_cast<const std::uint32_t*>(bytes + 0x30);
    const auto hi = *reinterpret_cast<const std::uint32_t*>(bytes + 0x34);
    if (explicitId != -1) {
        return { static_cast<std::uint32_t>(explicitId), 0xBE400000 };
    }
    if (lo != 0 || hi != 0) {
        return { lo, hi };
    }
    return {};
}

bool SameNetId(CharacterNetId a, CharacterNetId b) {
    return a.lo == b.lo && a.hi == b.hi;
}

bool IsAddonNetworkId(int networkId) {
    const auto& registry = Characters::Registry::Instance();
    return networkId >= registry.FirstAddonId() &&
           networkId < registry.FirstAddonId() + registry.AddonCount();
}

CharacterNetId AddonSyntheticSkinId(int networkId) {
    const auto& registry = Characters::Registry::Instance();
    if (!IsAddonNetworkId(networkId)) {
        return {};
    }
    return {
        static_cast<std::uint32_t>(networkId - registry.FirstAddonId() + 1),
        kAddonSkinIdHi
    };
}

void WriteCharacterSkinIdRaw(void* destination, CharacterNetId id) {
    auto* bytes = static_cast<std::uint8_t*>(destination);
    std::memcpy(bytes, &id.lo, sizeof(id.lo));
    std::memcpy(bytes + sizeof(id.lo), &id.hi, sizeof(id.hi));
}

void WriteRuntimeCharacterId(std::uint8_t* packet,
                             std::uint32_t idOffset,
                             std::uint32_t flagOffset,
                             int networkIndex,
                             void* characterRuntime,
                             void* lobbyManager,
                             ResolveCharacterSkinRuntimeFn resolveSkin,
                             bool allowAddonSyntheticId) {
    if (allowAddonSyntheticId && IsAddonNetworkId(networkIndex)) {
        if (characterRuntime) {
            const CharacterNetId id = AddonSyntheticSkinId(networkIndex);
            WriteCharacterSkinIdRaw(packet + idOffset + networkIndex * kCharacterIdBytes, id);
            packet[flagOffset + networkIndex] = 1;
        } else {
            WriteCharacterSkinIdRaw(packet + idOffset + networkIndex * kCharacterIdBytes, {});
            packet[flagOffset + networkIndex] = 0;
        }
        return;
    }

    void* skinRuntime = nullptr;
    if (characterRuntime) {
        skinRuntime = resolveSkin(lobbyManager, static_cast<std::uint8_t*>(characterRuntime) + 0x08);
    }

    WriteCharacterSkinId(packet + idOffset + networkIndex * kCharacterIdBytes, skinRuntime);
    packet[flagOffset + networkIndex] = skinRuntime
        ? *reinterpret_cast<std::uint8_t*>(static_cast<std::uint8_t*>(skinRuntime) + 0xBC)
        : 0;
}

ExpandedPacket BuildExpandedPacketFromRuntime() {
    const auto& registry = Characters::Registry::Instance();
    ExpandedPacket packet;
    packet.layout = CharacterSyncExpansion::ExpandedLayout();
    packet.bytes.resize(packet.layout.packetSize);
    packet.bytes[0] = 1;

    auto* lobbyManager = *reinterpret_cast<void**>(Core::Memory::Va(kLobbyManagerGlobalRva));
    if (!lobbyManager) {
        Core::Logger::Warn("BuildExpandedPacketFromRuntime: lobby manager is null");
        return packet;
    }

    auto resolveSkin = reinterpret_cast<ResolveCharacterSkinRuntimeFn>(
        NativeDB::NativeAddress(NativeDB::Natives::CC_ResolveCharacterSkinRuntime));
    auto* characterRuntimeArray = reinterpret_cast<void**>(
        static_cast<std::uint8_t*>(lobbyManager) + kCharacterRuntimeBaseArrayOffset);
    const std::uintptr_t workshopRuntimeArrayOffset =
        kCharacterRuntimeBaseArrayOffset + static_cast<std::uintptr_t>(registry.NetworkBaseCount()) * sizeof(void*);
    auto* workshopRuntimeArray = reinterpret_cast<void**>(
        static_cast<std::uint8_t*>(lobbyManager) + workshopRuntimeArrayOffset);

    const int baseCount = registry.NetworkBaseCount();
    Core::Logger::Info("Building expanded character sync packet: baseCount=%d totalCount=%d packetSize=%u workshopRuntimeOffset=0x%X",
                       baseCount,
                       registry.NetworkTotalCount(),
                       packet.layout.packetSize,
                       static_cast<unsigned int>(workshopRuntimeArrayOffset));
    for (int baseIndex = 0; baseIndex < baseCount; ++baseIndex) {
        const int networkIndex = baseIndex + 1;
        WriteRuntimeCharacterId(packet.bytes.data(),
                                packet.layout.primaryBaseIds,
                                packet.layout.primaryBaseFlags,
                                networkIndex,
                                characterRuntimeArray[baseIndex],
                                lobbyManager,
                                resolveSkin,
                                true);
        WriteRuntimeCharacterId(packet.bytes.data(),
                                packet.layout.secondaryBaseIds,
                                packet.layout.secondaryBaseFlags,
                                networkIndex,
                                characterRuntimeArray[baseIndex],
                                lobbyManager,
                                resolveSkin,
                                true);
    }

    for (int workshopIndex = 0; workshopIndex < static_cast<int>(kWorkshopEntries); ++workshopIndex) {
        WriteRuntimeCharacterId(packet.bytes.data(),
                                packet.layout.perPlayerWorkshopIds,
                                packet.layout.perPlayerWorkshopFlags,
                                workshopIndex,
                                workshopRuntimeArray[workshopIndex],
                                lobbyManager,
                                resolveSkin,
                                false);
    }

    return packet;
}

std::vector<std::uint8_t> BuildWireMessage(LobbyGameMode mode) {
    ExpandedPacket packet = BuildExpandedPacketFromRuntime();
    const std::uint16_t bodyLength = static_cast<std::uint16_t>(
        1 + 1 + kHookCrashersSyncMagic.size() + sizeof(std::uint16_t) * 2 + sizeof(std::uint32_t) + packet.bytes.size());

    std::vector<std::uint8_t> message;
    message.reserve(static_cast<std::size_t>(bodyLength) + sizeof(std::uint16_t));
    AppendUnaligned<std::uint16_t>(message, bodyLength);
    message.push_back(kCharacterSyncMessageType);
    message.push_back(static_cast<std::uint8_t>(mode));
    message.insert(message.end(), kHookCrashersSyncMagic.begin(), kHookCrashersSyncMagic.end());
    AppendUnaligned<std::uint16_t>(message, kHookCrashersSyncVersion);
    AppendUnaligned<std::uint16_t>(message, static_cast<std::uint16_t>(Characters::Registry::Instance().AddonCount()));
    AppendUnaligned<std::uint32_t>(message, static_cast<std::uint32_t>(packet.bytes.size()));
    message.insert(message.end(), packet.bytes.begin(), packet.bytes.end());
    return message;
}

bool TryReadWirePacket(std::uint8_t* payload, ExpandedPacket& packet) {
    if (!payload || payload[0] > 1) {
        return false;
    }

    // Wire framing (see BuildWireMessage): [u16 bodyLength][u8 type][payload...],
    // bodyLength counting from the type byte. The packet comes from another
    // player, so never read past what was actually received.
    constexpr std::size_t kHeaderBytes =
        1 + kHookCrashersSyncMagic.size() + sizeof(std::uint16_t) * 2 + sizeof(std::uint32_t);
    std::size_t available = 0;
    const bool framed = payload[-1] == kCharacterSyncMessageType;
    if (framed) {
        const std::uint16_t bodyLength = ReadUnaligned<std::uint16_t>(payload - 3);
        available = bodyLength > 0 ? bodyLength - 1u : 0u;
        if (available < kHeaderBytes) {
            Core::Logger::Warn("Rejected sync packet: %u payload bytes, header needs %u",
                               static_cast<unsigned>(available), static_cast<unsigned>(kHeaderBytes));
            return false;
        }
    }

    const std::uint8_t* cursor = payload + 1;
    if (!std::equal(kHookCrashersSyncMagic.begin(), kHookCrashersSyncMagic.end(), cursor)) {
        return false;
    }
    cursor += kHookCrashersSyncMagic.size();

    const auto version = ReadUnaligned<std::uint16_t>(cursor);
    cursor += sizeof(std::uint16_t);
    const auto addonCount = ReadUnaligned<std::uint16_t>(cursor);
    cursor += sizeof(std::uint16_t);
    const auto packetSize = ReadUnaligned<std::uint32_t>(cursor);
    cursor += sizeof(std::uint32_t);

    const auto& registry = Characters::Registry::Instance();
    const CharacterSyncPacketLayout expectedLayout = CharacterSyncExpansion::ExpandedLayout();
    if (version != kHookCrashersSyncVersion ||
        addonCount != static_cast<std::uint16_t>(registry.AddonCount()) ||
        packetSize != expectedLayout.packetSize) {
        Core::Logger::Warn("Rejected HC5 sync packet: version=%u addonCount=%u packetSize=%u expectedVersion=%u expectedAddons=%u expectedSize=%u",
                           version,
                           addonCount,
                           packetSize,
                           kHookCrashersSyncVersion,
                           static_cast<unsigned int>(registry.AddonCount()),
                           expectedLayout.packetSize);
        return false;
    }

    if (framed && available - kHeaderBytes < packetSize) {
        Core::Logger::Warn("Rejected HC5 sync packet: truncated body (%u of %u bytes)",
                           static_cast<unsigned>(available - kHeaderBytes), packetSize);
        return false;
    }

    packet.gameMode = static_cast<LobbyGameMode>(payload[0]);
    packet.layout = expectedLayout;
    packet.bytes.assign(cursor, cursor + packetSize);
    Core::Logger::Info("Accepted HC5 sync packet: addonCount=%u packetSize=%u", addonCount, packetSize);
    return true;
}

bool ApplyExpandedPacketToRoster(const ExpandedPacket& packet) {
    auto* lobbyManager = *reinterpret_cast<void**>(Core::Memory::Va(kLobbyManagerGlobalRva));
    if (!lobbyManager || packet.bytes.empty() || packet.bytes[0] == 0) {
        Core::Logger::Warn("ApplyExpandedPacketToRoster failed early: lobbyManager=%p bytes=%u firstByte=%u",
                           lobbyManager,
                           static_cast<unsigned int>(packet.bytes.size()),
                           packet.bytes.empty() ? 0 : packet.bytes[0]);
        return false;
    }

    auto insertEntry = reinterpret_cast<InsertCharacterSyncEntryFn>(Core::Memory::Va(kInsertCharacterSyncEntryRva));
    auto* sentinel = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(lobbyManager) + 0x1C);
    if (!sentinel || !insertEntry) {
        Core::Logger::Error("ApplyExpandedPacketToRoster missing sentinel/insertEntry sentinel=%p insertEntry=%p", sentinel, reinterpret_cast<void*>(insertEntry));
        return false;
    }

    const auto& registry = Characters::Registry::Instance();
    const int baseCount = registry.NetworkBaseCount();
    const int totalCount = registry.NetworkTotalCount();
    auto* characterRuntimeArray = reinterpret_cast<void**>(
        static_cast<std::uint8_t*>(lobbyManager) + kCharacterRuntimeBaseArrayOffset);
    int unavailableWorkshopCount = 0;
    int firstUnavailableWorkshopNetworkId = 0;
    CharacterNetId firstUnavailableWorkshopId {};
    std::uint8_t firstUnavailableWorkshopFlag = 0;
    auto insertBaseOrAddon = [&](int networkId, void* node, CharacterNetId id, std::uint8_t flag) -> bool {
        if (!node) {
            return false;
        }
        if (static_cast<std::uint8_t*>(node)[0x10] == 0) {
            static_cast<std::uint8_t*>(node)[0x10] = 1;
        }

        int key = networkId;
        InsertResult result {};
        Core::Logger::Info("ApplyExpandedPacketToRoster inserting base/addon networkId=%d node=%p map=%p flag=%u id=%08X:%08X",
                           networkId,
                           node,
                           static_cast<std::uint8_t*>(node) + 0x38,
                           flag,
                           id.hi,
                           id.lo);
        insertEntry(static_cast<std::uint8_t*>(node) + 0x38, &result, &key, &key, const_cast<std::uint8_t*>(packet.bytes.data()));
        return true;
    };

    auto findAndMark = [&](int networkId, CharacterNetId id, std::uint8_t flag) -> bool {
        const bool isAddonEntry = IsAddonNetworkId(networkId);
        if (isAddonEntry && SameNetId(id, AddonSyntheticSkinId(networkId))) {
            void* addonNode = characterRuntimeArray[networkId - 1];
            if (!addonNode) {
                Core::Logger::Warn("ApplyExpandedPacketToRoster addon runtime missing networkId=%d; deferring synthetic id=%08X:%08X",
                                   networkId,
                                   id.hi,
                                   id.lo);
                return true;
            }
            return insertBaseOrAddon(networkId, addonNode, id, flag);
        }

        if ((id.lo == 0 && id.hi == 0) && flag == 0) {
            return true;
        }

        const bool isWorkshopEntry = networkId > baseCount;
        for (auto* node = *reinterpret_cast<std::uint8_t**>(sentinel);
             node && node != sentinel;
             node = *reinterpret_cast<std::uint8_t**>(node)) {
            if (!SameNetId(RuntimeNodeSkinId(node), id)) {
                continue;
            }

            if (node[0x10] == 0) {
                node[0x10] = 1;
            }

            if (isWorkshopEntry) {
                Core::Logger::Info("ApplyExpandedPacketToRoster matched workshop networkId=%d node=%p flag=%u id=%08X:%08X",
                                   networkId,
                                   node,
                                   flag,
                                   id.hi,
                                   id.lo);
                return true;
            }

            return insertBaseOrAddon(networkId, node, id, flag);
        }

        // Steam Workshop subscriptions are per-user and are not an addon compatibility failure.
        if (isWorkshopEntry) {
            if (unavailableWorkshopCount == 0) {
                firstUnavailableWorkshopNetworkId = networkId;
                firstUnavailableWorkshopId = id;
                firstUnavailableWorkshopFlag = flag;
            }
            ++unavailableWorkshopCount;
            return true;
        }

        return flag == 0;
    };

    for (int networkId = 1; networkId <= totalCount; ++networkId) {
        CharacterNetId id {};
        std::uint8_t flag = 0;
        if (networkId <= baseCount) {
            id = ReadCharacterSkinId(packet.bytes.data(), packet.layout.primaryBaseIds, networkId);
            flag = packet.bytes[packet.layout.primaryBaseFlags + networkId];
        } else {
            const int workshopIndex = networkId - baseCount - 1;
            id = ReadCharacterSkinId(packet.bytes.data(), packet.layout.perPlayerWorkshopIds, workshopIndex);
            flag = packet.bytes[packet.layout.perPlayerWorkshopFlags + workshopIndex];
        }

        if (!findAndMark(networkId, id, flag)) {
            Core::Logger::Error("ApplyExpandedPacketToRoster missing runtime entry networkId=%d flag=%u id=%08X:%08X",
                                networkId,
                                flag,
                                id.hi,
                                id.lo);
            return false;
        }
    }

    if (unavailableWorkshopCount > 0) {
        Core::Logger::Warn("ApplyExpandedPacketToRoster ignored unavailable workshop entries count=%d firstNetworkId=%d firstFlag=%u firstId=%08X:%08X",
                           unavailableWorkshopCount,
                           firstUnavailableWorkshopNetworkId,
                           firstUnavailableWorkshopFlag,
                           firstUnavailableWorkshopId.hi,
                           firstUnavailableWorkshopId.lo);
    }
    Core::Logger::Info("ApplyExpandedPacketToRoster succeeded totalCount=%d unavailableWorkshop=%d",
                       totalCount,
                       unavailableWorkshopCount);
    return true;
}

void __fastcall DetouredSendInitialCharacterSyncPacket(void* self, void*, std::uint32_t* outResult, void* peer) {
    LobbyGameMode localMode;
    if (!ReadLocalGameMode(localMode)) {
        RejectCharacterSync(self,outResult,"local game mode unavailable",LobbyGameMode::Unknown,LobbyGameMode::Unknown);
        return;
    }
    if (!CharacterSyncExpansion::NeedsExpandedLayout()) {
        g_OriginalSendInitialCharacterSyncPacket(self, outResult, peer);
        return;
    }

    Core::Logger::Info("DetouredSendInitialCharacterSyncPacket self=%p peer=%p", self, peer);

    auto* session = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(self) + 0x0C);
    if (!session) {
        Core::Logger::Error("SendInitialCharacterSyncPacket: session is null");
        SetResult(outResult, 1);
        return;
    }

    auto channelForLocalPlayer = reinterpret_cast<ChannelForLocalPlayerFn>(GetVfunc(session, 0x4C));
    auto* channel = channelForLocalPlayer
        ? channelForLocalPlayer(session, *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(self) + 0x70))
        : nullptr;
    if (!channel) {
        Core::Logger::Error("SendInitialCharacterSyncPacket: channel is null");
        SetResult(outResult, 1);
        return;
    }

    std::vector<std::uint8_t> message = BuildWireMessage(localMode);
    if (CallPeerBool(peer, 0x1C)) {
        Core::Logger::Info("SendInitialCharacterSyncPacket loopback/local peer");
        DetouredHandleIncomingCharacterSyncPacket(self, nullptr, outResult, channel, message.data() + 3);
        return;
    }

    auto send = reinterpret_cast<ChannelSendFn>(GetVfunc(channel, 0x64));
    if (!send) {
        Core::Logger::Error("SendInitialCharacterSyncPacket: send vfunc is null");
        SetResult(outResult, 1);
        return;
    }

    std::uint32_t scratch[2] {};
    send(channel, scratch, peer, message.data(), static_cast<std::uint32_t>(message.size()), 1);
    Core::Logger::Info("Sent HC5 sync message bytes=%u", static_cast<unsigned int>(message.size()));
    SetResult(outResult, 0);
}

void __fastcall DetouredHandleIncomingCharacterSyncPacket(void* self,
                                                          void*,
                                                          std::uint32_t* outResult,
                                                          void* transport,
                                                          std::uint8_t* payload) {
    Core::Logger::Info("DetouredHandleIncomingCharacterSyncPacket self=%p transport=%p payload=%p", self, transport, payload);

    ExpandedPacket packet;
    if (!TryReadWirePacket(payload, packet)) {
        // A recognized HC envelope with an old version/wrong roster is NOT a
        // vanilla packet. Forwarding it would bypass checks and misread its body.
        if (payload && std::equal(kHookCrashersSyncMagic.begin(),kHookCrashersSyncMagic.end(),payload+1)) {
            LobbyGameMode localMode; ReadLocalGameMode(localMode);
            RejectCharacterSync(self,outResult,"incompatible HookCrashers protocol or addon layout",localMode,LobbyGameMode::Unknown);
            return;
        }
        Core::Logger::Warn("Incoming packet is not HC5 expanded packet; forwarding to vanilla handler");
        g_OriginalHandleIncomingCharacterSyncPacket(self, outResult, transport, payload);
        return;
    }

    if (!ApplyAuthoritativeGameMode(self,transport,packet.gameMode)) {
        LobbyGameMode localMode; ReadLocalGameMode(localMode);
        RejectCharacterSync(self,outResult,"could not apply host game mode",localMode,packet.gameMode);
        return;
    }

    if (ApplyExpandedPacketToRoster(packet)) {
        *reinterpret_cast<std::uint8_t*>(static_cast<std::uint8_t*>(self) + 0x29A5) = 1;
        SetResult(outResult, 0);
        Core::Logger::Info("Handled HC5 expanded sync packet successfully");
        return;
    }

    *reinterpret_cast<std::uint32_t*>(Core::Memory::Va(kNetworkKickReasonGlobalRva)) = 0x0D;
    Core::Logger::Error("Failed HC5 expanded sync packet; set kick reason 0x0D");
    SetResult(outResult, 1);
}

bool InstallNetworkDetours() {
    const auto* sender = reinterpret_cast<const std::uint8_t*>(
        NativeDB::NativeAddress(NativeDB::Natives::CC_SendInitialCharacterSyncPacket));
    const std::uint8_t senderPrefix[] {0x55,0x8B,0xEC,0xB8,0xF4,0x14,0,0};
    const std::uint8_t modeMapOffset[] {0x81,0xC1,0x14,0x08,0,0};
    const std::uint8_t modeWrite[] {0x88,0x85,0x13,0xEB,0xFF,0xFF};
    std::uint32_t modeProfile = 0;
    std::memcpy(&modeProfile,sender+0x51,sizeof(modeProfile));
    if (std::memcmp(sender,senderPrefix,sizeof(senderPrefix)) || sender[0x4F]!=0x8B || sender[0x50]!=0x0D ||
        modeProfile!=Core::Memory::Va(0x391294) ||
        std::memcmp(sender+0x5B,modeMapOffset,sizeof(modeMapOffset)) ||
        std::memcmp(sender+0x8A,modeWrite,sizeof(modeWrite))) {
        Core::Logger::Error("Character sync install refused: unsupported Classic/Fresh mode layout");
        return false;
    }
    const auto* setter = reinterpret_cast<const std::uint8_t*>(Core::Memory::Va(kSetFreshModeRva));
    const std::uint8_t setterPrefix[] {0x55,0x8B,0xEC,0x83,0xEC,0x0C,0x8B,0x0D};
    const std::uint8_t setterTail[] {0x89,0x48,0x14,0x8B,0xE5,0x5D,0xC2,0x04,0x00};
    std::uint32_t setterProfile=0;
    std::memcpy(&setterProfile,setter+8,sizeof(setterProfile));
    if (std::memcmp(setter,setterPrefix,sizeof(setterPrefix)) || setterProfile!=Core::Memory::Va(0x391294) ||
        std::memcmp(setter+0x31,setterTail,sizeof(setterTail))) {
        Core::Logger::Error("Character sync install refused: unsupported host-mode setter"); return false;
    }
    g_SetFreshMode = reinterpret_cast<SetFreshModeFn>(Core::Memory::Va(kSetFreshModeRva));
    const std::uint8_t insertPrefix[] { 0x55, 0x8B, 0xEC, 0x6A, 0xFF, 0x68 };
    const std::uint8_t insertReturn[] { 0xC2, 0x10, 0x00 };
    const auto* insert = reinterpret_cast<const std::uint8_t*>(Core::Memory::Va(kInsertCharacterSyncEntryRva));
    if (std::memcmp(insert, insertPrefix, sizeof(insertPrefix)) != 0 ||
        std::memcmp(insert + 0xE9, insertReturn, sizeof(insertReturn)) != 0) {
        Core::Logger::Error("Character sync install refused: unsupported roster map insert instructions");
        return false;
    }
    g_OriginalSendInitialCharacterSyncPacket =
        reinterpret_cast<SendInitialCharacterSyncPacketFn>(
            NativeDB::NativeAddress(NativeDB::Natives::CC_SendInitialCharacterSyncPacket));
    g_OriginalHandleIncomingCharacterSyncPacket =
        reinterpret_cast<HandleIncomingCharacterSyncPacketFn>(
            NativeDB::NativeAddress(NativeDB::Natives::CC_HandleIncomingCharacterSyncPacket));
    Core::Logger::Info("Installing network detours send=%p recv=%p",
                       reinterpret_cast<void*>(g_OriginalSendInitialCharacterSyncPacket),
                       reinterpret_cast<void*>(g_OriginalHandleIncomingCharacterSyncPacket));

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG result = DetourAttach(reinterpret_cast<PVOID*>(&g_OriginalSendInitialCharacterSyncPacket),
                               DetouredSendInitialCharacterSyncPacket);
    if (result == NO_ERROR) {
        result = DetourAttach(reinterpret_cast<PVOID*>(&g_OriginalHandleIncomingCharacterSyncPacket),
                              DetouredHandleIncomingCharacterSyncPacket);
    }
    if (result != NO_ERROR) {
        Core::Logger::Error("InstallNetworkDetours failed result=%ld", result);
        DetourTransactionAbort();
        return false;
    }
    const LONG commit = DetourTransactionCommit();
    Core::Logger::Info("InstallNetworkDetours commit result=%ld", commit);
    return commit == NO_ERROR;
}

} // namespace

constexpr CharacterSyncPacketLayout CharacterSyncExpansion::VanillaLayout() {
    return {
        0x430,
        0x008,
        0x110,
        0x118,
        0x168,
        0x189,
        0x198,
        0x2A0,
        0x3E0,
        0x401
    };
}

CharacterSyncPacketLayout CharacterSyncExpansion::ExpandedLayout() {
    const auto& registry = Characters::Registry::Instance();
    const auto addonCount = static_cast<std::uint32_t>(registry.AddonCount());
    const auto baseEntriesWithDummy = kVanillaBaseEntriesWithDummy + addonCount;

    CharacterSyncPacketLayout layout {};
    layout.primaryBaseIds = 0x008;
    layout.playerMask = layout.primaryBaseIds + baseEntriesWithDummy * kCharacterIdBytes;
    layout.selectedWorkshopIds = layout.playerMask + 0x008;
    layout.primaryBaseFlags = layout.selectedWorkshopIds + kWorkshopEntriesPerPlayer * kCharacterIdBytes;
    layout.selectedWorkshopFlags = layout.primaryBaseFlags + baseEntriesWithDummy;
    layout.secondaryBaseIds = layout.selectedWorkshopFlags + kWorkshopEntriesPerPlayer;
    layout.perPlayerWorkshopIds = layout.secondaryBaseIds + baseEntriesWithDummy * kCharacterIdBytes;
    layout.secondaryBaseFlags = layout.perPlayerWorkshopIds + kWorkshopEntries * kCharacterIdBytes;
    layout.perPlayerWorkshopFlags = layout.secondaryBaseFlags + baseEntriesWithDummy;
    layout.packetSize = layout.perPlayerWorkshopFlags + kWorkshopEntries;
    return layout;
}

bool CharacterSyncExpansion::NeedsExpandedLayout() {
    return Characters::Registry::Instance().AddonCount() > 0;
}

bool CharacterSyncExpansion::Install() {
    if (!NeedsExpandedLayout()) {
        Core::Logger::Info("CharacterSyncExpansion: native mode synchronization retained (no addons)");
        return true;
    }

    // Ghidra evidence:
    // - CC_BuildCharacterSyncPacket compares the base loop against 0x21 at RVA 0x88A85.
    // - CC_ApplyCharacterSyncPacketToRoster compares base/workshop split at RVA 0x877B2
    //   and total stream end at RVA 0x87A1B.
    // - Those compares are not enough. The packet also hardcodes offsets 0x110, 0x118,
    //   0x168, 0x189, 0x198, 0x2A0, 0x3E0 and 0x401, plus stack copy sizes 0x190
    //   and 0x430 in CC_SendInitialCharacterSyncPacket / CC_HandleIncomingCharacterSyncPacket.
    //
    // The correct HookCrashers v5 packet is larger than the vanilla 0x430-byte stack
    // buffers when addons are present because it preserves both:
    // - expanded base characters: 32 vanilla + N addons
    // - workshop characters: 10 per local player, 40 total
    //
    // CC_SendInitialCharacterSyncPacket / CC_HandleIncomingCharacterSyncPacket are detoured
    // below so the expanded packet is heap-backed and never written into the vanilla stack
    // buffers. The wire body carries magic/version/addon-count/packet-size before the packet.
    CharacterSyncPacketLayout layout = ExpandedLayout();
    Core::Logger::Info("CharacterSyncExpansion install: packetSize=%u baseIds=0x%X playerMask=0x%X selectedWorkshop=0x%X primaryFlags=0x%X secondaryIds=0x%X perPlayerWorkshop=0x%X secondaryFlags=0x%X perPlayerFlags=0x%X",
                       layout.packetSize,
                       layout.primaryBaseIds,
                       layout.playerMask,
                       layout.selectedWorkshopIds,
                       layout.primaryBaseFlags,
                       layout.secondaryBaseIds,
                       layout.perPlayerWorkshopIds,
                       layout.secondaryBaseFlags,
                       layout.perPlayerWorkshopFlags);
    return InstallNetworkDetours();
}

bool CharacterSyncExpansion::Remove() {
    if (!g_OriginalSendInitialCharacterSyncPacket && !g_OriginalHandleIncomingCharacterSyncPacket) {
        return true;
    }
    Core::Logger::Info("Removing CharacterSyncExpansion detours");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    if (g_OriginalSendInitialCharacterSyncPacket) {
        DetourDetach(reinterpret_cast<PVOID*>(&g_OriginalSendInitialCharacterSyncPacket),
                     DetouredSendInitialCharacterSyncPacket);
    }
    if (g_OriginalHandleIncomingCharacterSyncPacket) {
        DetourDetach(reinterpret_cast<PVOID*>(&g_OriginalHandleIncomingCharacterSyncPacket),
                     DetouredHandleIncomingCharacterSyncPacket);
    }
    DetourTransactionCommit();
    g_OriginalSendInitialCharacterSyncPacket = nullptr;
    g_OriginalHandleIncomingCharacterSyncPacket = nullptr;
    Core::Logger::Info("CharacterSyncExpansion detours removed");
    return true;
}

} // namespace HookCrashers::Network
