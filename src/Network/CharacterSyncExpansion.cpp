#include "CharacterSyncExpansion.h"

#include "../Characters/CharacterRegistry.h"
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
constexpr std::uintptr_t kLobbyManagerGlobalRva = 0x3912B8;
constexpr std::uintptr_t kCharacterRuntimeBaseArrayOffset = 0x11AC;
constexpr std::uintptr_t kWorkshopRuntimeArrayOffset = 0x122C;
constexpr std::uintptr_t kNetworkKickReasonGlobalRva = 0x260E94;
constexpr std::uint8_t kCharacterSyncMessageType = 0x0D;
constexpr std::uint16_t kHookCrashersSyncVersion = 5;
constexpr std::array<std::uint8_t, 4> kHookCrashersSyncMagic { 'H', 'C', '5', 'C' };

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
using InsertCharacterSyncEntryFn = void*(__thiscall*)(void* map,
                                                      void* outPair,
                                                      void* keyForSearch,
                                                      void* keyForInsert,
                                                      void* packet);

SendInitialCharacterSyncPacketFn g_OriginalSendInitialCharacterSyncPacket = nullptr;
HandleIncomingCharacterSyncPacketFn g_OriginalHandleIncomingCharacterSyncPacket = nullptr;

void __fastcall DetouredHandleIncomingCharacterSyncPacket(void* self,
                                                          void*,
                                                          std::uint32_t* outResult,
                                                          void* transport,
                                                          std::uint8_t* payload);

struct ExpandedPacket {
    CharacterSyncPacketLayout layout {};
    std::vector<std::uint8_t> bytes;
};

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

void WriteRuntimeCharacterId(std::uint8_t* packet,
                             std::uint32_t idOffset,
                             std::uint32_t flagOffset,
                             int networkIndex,
                             void* characterRuntime,
                             void* lobbyManager,
                             ResolveCharacterSkinRuntimeFn resolveSkin) {
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
        return packet;
    }

    auto resolveSkin = reinterpret_cast<ResolveCharacterSkinRuntimeFn>(
        NativeDB::NativeAddress(NativeDB::Natives::CC_ResolveCharacterSkinRuntime));
    auto* characterRuntimeArray = reinterpret_cast<void**>(
        static_cast<std::uint8_t*>(lobbyManager) + kCharacterRuntimeBaseArrayOffset);
    auto* workshopRuntimeArray = reinterpret_cast<void**>(
        static_cast<std::uint8_t*>(lobbyManager) + kWorkshopRuntimeArrayOffset);

    const int baseCount = registry.NetworkBaseCount();
    for (int baseIndex = 0; baseIndex < baseCount; ++baseIndex) {
        const int networkIndex = baseIndex + 1;
        WriteRuntimeCharacterId(packet.bytes.data(),
                                packet.layout.primaryBaseIds,
                                packet.layout.primaryBaseFlags,
                                networkIndex,
                                characterRuntimeArray[baseIndex],
                                lobbyManager,
                                resolveSkin);
        WriteRuntimeCharacterId(packet.bytes.data(),
                                packet.layout.secondaryBaseIds,
                                packet.layout.secondaryBaseFlags,
                                networkIndex,
                                characterRuntimeArray[baseIndex],
                                lobbyManager,
                                resolveSkin);
    }

    for (int workshopIndex = 0; workshopIndex < static_cast<int>(kWorkshopEntries); ++workshopIndex) {
        WriteRuntimeCharacterId(packet.bytes.data(),
                                packet.layout.perPlayerWorkshopIds,
                                packet.layout.perPlayerWorkshopFlags,
                                workshopIndex,
                                workshopRuntimeArray[workshopIndex],
                                lobbyManager,
                                resolveSkin);
    }

    return packet;
}

std::vector<std::uint8_t> BuildWireMessage() {
    ExpandedPacket packet = BuildExpandedPacketFromRuntime();
    const std::uint16_t bodyLength = static_cast<std::uint16_t>(
        1 + 1 + kHookCrashersSyncMagic.size() + sizeof(std::uint16_t) * 2 + sizeof(std::uint32_t) + packet.bytes.size());

    std::vector<std::uint8_t> message;
    message.reserve(static_cast<std::size_t>(bodyLength) + sizeof(std::uint16_t));
    AppendUnaligned<std::uint16_t>(message, bodyLength);
    message.push_back(kCharacterSyncMessageType);
    message.push_back(1);
    message.insert(message.end(), kHookCrashersSyncMagic.begin(), kHookCrashersSyncMagic.end());
    AppendUnaligned<std::uint16_t>(message, kHookCrashersSyncVersion);
    AppendUnaligned<std::uint16_t>(message, static_cast<std::uint16_t>(Characters::Registry::Instance().AddonCount()));
    AppendUnaligned<std::uint32_t>(message, static_cast<std::uint32_t>(packet.bytes.size()));
    message.insert(message.end(), packet.bytes.begin(), packet.bytes.end());
    return message;
}

bool TryReadWirePacket(std::uint8_t* payload, ExpandedPacket& packet) {
    if (!payload || payload[0] == 0) {
        return false;
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
        return false;
    }

    packet.layout = expectedLayout;
    packet.bytes.assign(cursor, cursor + packetSize);
    return true;
}

bool ApplyExpandedPacketToRoster(const ExpandedPacket& packet) {
    auto* lobbyManager = *reinterpret_cast<void**>(Core::Memory::Va(kLobbyManagerGlobalRva));
    if (!lobbyManager || packet.bytes.empty() || packet.bytes[0] == 0) {
        return false;
    }

    auto insertEntry = reinterpret_cast<InsertCharacterSyncEntryFn>(Core::Memory::Va(0x12B030));
    auto* sentinel = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(lobbyManager) + 0x1C);
    if (!sentinel || !insertEntry) {
        return false;
    }

    const auto& registry = Characters::Registry::Instance();
    const int baseCount = registry.NetworkBaseCount();
    const int totalCount = registry.NetworkTotalCount();
    auto findAndMark = [&](int networkId, CharacterNetId id, std::uint8_t flag) -> bool {
        if ((id.lo == 0 && id.hi == 0) && flag == 0) {
            return true;
        }

        for (auto* node = *reinterpret_cast<std::uint8_t**>(sentinel);
             node && node != sentinel;
             node = *reinterpret_cast<std::uint8_t**>(node)) {
            if (!SameNetId(RuntimeNodeSkinId(node), id)) {
                continue;
            }

            if (node[0x10] == 0) {
                node[0x10] = 1;
            }

            int key = networkId;
            InsertResult result {};
            insertEntry(node + 0x38, &result, &key, &key, const_cast<std::uint8_t*>(packet.bytes.data()));
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
            return false;
        }
    }

    return true;
}

void __fastcall DetouredSendInitialCharacterSyncPacket(void* self, void*, std::uint32_t* outResult, void* peer) {
    if (!CharacterSyncExpansion::NeedsExpandedLayout()) {
        g_OriginalSendInitialCharacterSyncPacket(self, outResult, peer);
        return;
    }

    auto* session = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(self) + 0x0C);
    if (!session) {
        SetResult(outResult, 1);
        return;
    }

    auto channelForLocalPlayer = reinterpret_cast<ChannelForLocalPlayerFn>(GetVfunc(session, 0x4C));
    auto* channel = channelForLocalPlayer
        ? channelForLocalPlayer(session, *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(self) + 0x70))
        : nullptr;
    if (!channel) {
        SetResult(outResult, 1);
        return;
    }

    std::vector<std::uint8_t> message = BuildWireMessage();
    if (CallPeerBool(peer, 0x1C)) {
        DetouredHandleIncomingCharacterSyncPacket(self, nullptr, outResult, channel, message.data() + 3);
        return;
    }

    auto send = reinterpret_cast<ChannelSendFn>(GetVfunc(channel, 0x64));
    if (!send) {
        SetResult(outResult, 1);
        return;
    }

    std::uint32_t scratch[2] {};
    send(channel, scratch, peer, message.data(), static_cast<std::uint32_t>(message.size()), 1);
    SetResult(outResult, 0);
}

void __fastcall DetouredHandleIncomingCharacterSyncPacket(void* self,
                                                          void*,
                                                          std::uint32_t* outResult,
                                                          void* transport,
                                                          std::uint8_t* payload) {
    if (!CharacterSyncExpansion::NeedsExpandedLayout()) {
        g_OriginalHandleIncomingCharacterSyncPacket(self, outResult, transport, payload);
        return;
    }

    ExpandedPacket packet;
    if (!TryReadWirePacket(payload, packet)) {
        g_OriginalHandleIncomingCharacterSyncPacket(self, outResult, transport, payload);
        return;
    }

    (void)transport;

    if (ApplyExpandedPacketToRoster(packet)) {
        *reinterpret_cast<std::uint8_t*>(static_cast<std::uint8_t*>(self) + 0x29A5) = 1;
        SetResult(outResult, 0);
        return;
    }

    *reinterpret_cast<std::uint32_t*>(Core::Memory::Va(kNetworkKickReasonGlobalRva)) = 0x0D;
    SetResult(outResult, 1);
}

bool InstallNetworkDetours() {
    g_OriginalSendInitialCharacterSyncPacket =
        reinterpret_cast<SendInitialCharacterSyncPacketFn>(
            NativeDB::NativeAddress(NativeDB::Natives::CC_SendInitialCharacterSyncPacket));
    g_OriginalHandleIncomingCharacterSyncPacket =
        reinterpret_cast<HandleIncomingCharacterSyncPacketFn>(
            NativeDB::NativeAddress(NativeDB::Natives::CC_HandleIncomingCharacterSyncPacket));

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG result = DetourAttach(reinterpret_cast<PVOID*>(&g_OriginalSendInitialCharacterSyncPacket),
                               DetouredSendInitialCharacterSyncPacket);
    if (result == NO_ERROR) {
        result = DetourAttach(reinterpret_cast<PVOID*>(&g_OriginalHandleIncomingCharacterSyncPacket),
                              DetouredHandleIncomingCharacterSyncPacket);
    }
    if (result != NO_ERROR) {
        DetourTransactionAbort();
        return false;
    }
    return DetourTransactionCommit() == NO_ERROR;
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
    return InstallNetworkDetours();
}

bool CharacterSyncExpansion::Remove() {
    if (!g_OriginalSendInitialCharacterSyncPacket && !g_OriginalHandleIncomingCharacterSyncPacket) {
        return true;
    }

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
    return true;
}

} // namespace HookCrashers::Network
