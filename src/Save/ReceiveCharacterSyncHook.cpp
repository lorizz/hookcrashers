#include "ReceiveCharacterSyncHook.h"

#include "CharacterConfig.h"
#include "../Util/Logger.h"
#include "../../include/HookCrashers/Public/Globals.h"

#include <windows.h>
#include <detours.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace HookCrashers::Save {
	namespace {
		constexpr uintptr_t kReceiveCharacterSyncRva = 0xF2310;
		constexpr uintptr_t kClearCharacterSyncPacketRva = 0x5FD40;
		constexpr uintptr_t kCopyVanillaBlocksRva = 0x5FE00;
		constexpr uintptr_t kSyncFreshBlocksRva = 0x5FE70;
		constexpr uintptr_t kMergeVanillaBlocksRva = 0x5FF20;
		constexpr uintptr_t kMergeCharacterSyncDataRva = 0x5FFD0;
		constexpr uintptr_t kMergeSelectedBlocksRva = 0x60040;
		constexpr uintptr_t kApplyCharacterSyncPacketRva = 0x87680;
		constexpr uintptr_t kBuildCharacterSyncPacketRva = 0x886D0;
		constexpr uintptr_t kClearCharacterTableRva = 0x846E0;
		constexpr uintptr_t kSetRemoteCharacterSyncStateRva = 0x10A4B0;
		constexpr uintptr_t kReadTypedValueRva = 0x3F020;
		constexpr uintptr_t kGlobalDword4912A0Rva = 0x3912A0;
		constexpr uintptr_t kGlobalLobbyManagerRva = 0x3912B8;
		constexpr uintptr_t kGlobalDword360E94Rva = 0x260E94;

		// First real detour test: replace ReceiveCharacterSync with an equivalent
		// implementation that uses heap packet buffers. Keep packet layout vanilla
		// for this pass; expanded addon sync gets moved here after this is stable.
		constexpr bool kUseReceiveCharacterSyncReplacement = false;
		constexpr bool kInstallReceiveCharacterSyncHook = true;

		constexpr size_t kVanillaPacketSize = 0x430;
		constexpr size_t kVanillaFirstBlockOffset = 0x008;
		constexpr size_t kVanillaFirstBlockBytes = 0x108;
		constexpr size_t kVanillaPlayerMaskOffset = 0x110;
		constexpr size_t kVanillaSelectedBlockOffset = 0x118;
		constexpr size_t kVanillaSelectedBlockBytes = 0x50;
		constexpr size_t kVanillaFirstFlagsOffset = 0x168;
		constexpr size_t kVanillaSelectedFlagsOffset = 0x189;
		constexpr size_t kVanillaSecondBlockOffset = 0x198;
		constexpr size_t kVanillaFinalBlockOffset = 0x2A0;
		constexpr size_t kVanillaFinalBlockBytes = 0x140;
		constexpr size_t kVanillaFinalFlagsOffset = 0x401;
		constexpr bool kUseExpandedWirePayload = true;

		struct ExpandedPacketLayout {
			size_t packetSize = kVanillaPacketSize;
			size_t firstBlockOffset = kVanillaFirstBlockOffset;
			size_t firstBlockBytes = kVanillaFirstBlockBytes;
			size_t playerMaskOffset = kVanillaPlayerMaskOffset;
			size_t selectedBlockOffset = kVanillaSelectedBlockOffset;
			size_t firstFlagsOffset = kVanillaFirstFlagsOffset;
			size_t selectedFlagsOffset = kVanillaSelectedFlagsOffset;
			size_t secondBlockOffset = kVanillaSecondBlockOffset;
			size_t finalBlockOffset = kVanillaFinalBlockOffset;
			size_t finalTailOffset = kVanillaFinalFlagsOffset;
		};

		using ReceiveCharacterSyncFn = uint32_t*(__thiscall*)(void* self, uint32_t* outResult, void* peerOrSession, uint8_t* incomingPacket);
		using ClearCharacterSyncPacketFn = void*(__thiscall*)(void* packet);
		using CopyVanillaBlocksFn = void*(__thiscall*)(void* outPacket, void* srcPacket);
		using MergePacketFn = int(__thiscall*)(void* outPacket, void* srcPacket);
		using MergeSelectedBlocksFn = int(__thiscall*)(void* outPacket, uint8_t playerMask, void* srcPacket);
		using BuildCharacterSyncPacketFn = void*(__stdcall*)(void* outPacket, uint8_t includeFreshData, uint8_t playerMask);
		using ApplyCharacterSyncPacketFn = uint8_t(__stdcall*)(void* packet);
		using ClearCharacterTableFn = void(__thiscall*)(void* lobbyManager);
		using SetRemoteCharacterSyncStateFn = void(__stdcall*)(int enabled);
		using ReadTypedValueFn = void(__thiscall*)(void* reader, int* outValue, int* id);

		ReceiveCharacterSyncFn g_originalReceiveCharacterSync = nullptr;
		ClearCharacterSyncPacketFn g_clearCharacterSyncPacket = nullptr;
		CopyVanillaBlocksFn g_copyVanillaBlocks = nullptr;
		MergePacketFn g_syncFreshBlocks = nullptr;
		MergePacketFn g_mergeVanillaBlocks = nullptr;
		MergePacketFn g_mergeCharacterSyncData = nullptr;
		MergeSelectedBlocksFn g_mergeSelectedBlocks = nullptr;
		BuildCharacterSyncPacketFn g_buildCharacterSyncPacket = nullptr;
		ApplyCharacterSyncPacketFn g_applyCharacterSyncPacket = nullptr;
		ClearCharacterTableFn g_clearCharacterTable = nullptr;
		SetRemoteCharacterSyncStateFn g_setRemoteCharacterSyncState = nullptr;
		ReadTypedValueFn g_readTypedValue = nullptr;

		ExpandedPacketLayout GetExpandedPacketLayout(int addonCount) {
			ExpandedPacketLayout layout;
			if (addonCount <= 0) {
				return layout;
			}

			const size_t addedIdBytes = static_cast<size_t>(addonCount) * 8;
			const size_t addedFlagBytes = static_cast<size_t>(addonCount);

			layout.firstBlockBytes = kVanillaFirstBlockBytes + addedIdBytes;
			layout.playerMaskOffset = kVanillaPlayerMaskOffset + addedIdBytes;
			layout.selectedBlockOffset = kVanillaSelectedBlockOffset + addedIdBytes;
			layout.firstFlagsOffset = kVanillaFirstFlagsOffset + addedIdBytes;
			layout.selectedFlagsOffset = kVanillaSelectedFlagsOffset + addedIdBytes + addedFlagBytes;
			layout.secondBlockOffset = kVanillaSecondBlockOffset + addedIdBytes + addedFlagBytes;
			layout.finalBlockOffset = kVanillaFinalBlockOffset + addedIdBytes + addedFlagBytes;
			layout.finalTailOffset = layout.finalBlockOffset + kVanillaFinalBlockBytes + 1;
			layout.packetSize = layout.finalTailOffset + 0x30;
			return layout;
		}

		bool IsPeerCompatibleForExpandedSync(void* peerOrSession, const uint8_t* incomingPacket) {
			// TODO: replace this with the manifest handshake. This is where we decide:
			// - vanilla peer: use vanilla packet/original function
			// - same HookCrashers manifest: use expanded packet replacement
			// - different manifest: reject or mark outResult as failed
			(void)peerOrSession;
			(void)incomingPacket;
			return true;
		}

		void* ReadPtr(void* base, size_t offset) {
			return *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(base) + offset);
		}

		uint8_t ReadU8(void* base, size_t offset) {
			return *reinterpret_cast<uint8_t*>(reinterpret_cast<uint8_t*>(base) + offset);
		}

		void WriteU8(void* base, size_t offset, uint8_t value) {
			*reinterpret_cast<uint8_t*>(reinterpret_cast<uint8_t*>(base) + offset) = value;
		}

		bool CallBool0(void* object, size_t vtableOffset) {
			using Fn = uint8_t(__thiscall*)(void*);
			if (!object) {
				return false;
			}
			Fn fn = *reinterpret_cast<Fn*>(*reinterpret_cast<uintptr_t*>(object) + vtableOffset);
			return fn(object) != 0;
		}

		bool CallBool1(void* object, size_t vtableOffset, void* arg0) {
			using Fn = uint8_t(__thiscall*)(void*, void*);
			if (!object) {
				return false;
			}
			Fn fn = *reinterpret_cast<Fn*>(*reinterpret_cast<uintptr_t*>(object) + vtableOffset);
			return fn(object, arg0) != 0;
		}

		int CallInt0(void* object, size_t vtableOffset) {
			using Fn = int(__thiscall*)(void*);
			if (!object) {
				return 0;
			}
			Fn fn = *reinterpret_cast<Fn*>(*reinterpret_cast<uintptr_t*>(object) + vtableOffset);
			return fn(object);
		}

		uint8_t CallU8_0(void* object, size_t vtableOffset) {
			using Fn = uint8_t(__thiscall*)(void*);
			if (!object) {
				return 0;
			}
			Fn fn = *reinterpret_cast<Fn*>(*reinterpret_cast<uintptr_t*>(object) + vtableOffset);
			return fn(object);
		}
		void* CallPtr1UInt(void* object, size_t vtableOffset, unsigned int arg0) {
			using Fn = void*(__thiscall*)(void*, unsigned int);
			if (!object) {
				return nullptr;
			}
			Fn fn = *reinterpret_cast<Fn*>(*reinterpret_cast<uintptr_t*>(object) + vtableOffset);
			return fn(object, arg0);
		}

		bool HasTypedValue(int id) {
			void* global4912A0 = *reinterpret_cast<void**>(g_moduleBase + kGlobalDword4912A0Rva);
			if (!global4912A0 || !g_readTypedValue) {
				return false;
			}

			int outValue = 0;
			int queryId = id;
			g_readTypedValue(reinterpret_cast<uint8_t*>(global4912A0) + 0x814, &outValue, &queryId);
			if (!outValue) {
				return false;
			}
			return *reinterpret_cast<uint32_t*>(outValue + 0x14) != 0;
		}

		void SetResult(uint32_t* outResult, uint32_t value) {
			if (!outResult) {
				return;
			}
			outResult[0] = value;
			outResult[1] = 0;
		}

		uint32_t* RunReceiveReplacement(void* self, uint32_t* outResult, void* peerOrSession, uint8_t* incomingPacket, const ExpandedPacketLayout& layout) {
			if (!self || !outResult || !peerOrSession || !incomingPacket) {
				SetResult(outResult, 0);
				return outResult;
			}

			void* lobbyState = ReadPtr(self, 0x1C);
			if (lobbyState && *reinterpret_cast<uint16_t*>(lobbyState) == 5) {
				SetResult(outResult, 1);
				return outResult;
			}

			if (!ReadU8(self, 0x29A4)) {
				SetResult(outResult, 0);
				return outResult;
			}

			const bool peerIsHost = CallBool0(peerOrSession, 0x20);
			void* session = ReadPtr(self, 0x0C);
			if (peerIsHost && (!session || !CallBool0(session, 0x68))) {
				g_setRemoteCharacterSyncState(incomingPacket[0] != 0);
			}

			uint8_t forceEmptyIncoming = 0;
			if ((!peerIsHost && !ReadU8(self, 0x29A5)) || HasTypedValue(0x120)) {
				forceEmptyIncoming = 0;
			} else {
				forceEmptyIncoming = 1;
			}

			const size_t incomingPayloadBytes = kUseExpandedWirePayload
				? (layout.secondBlockOffset - layout.firstBlockOffset)
				: (kVanillaSecondBlockOffset - kVanillaFirstBlockOffset);

			std::vector<uint8_t> srcPacket(layout.packetSize);
			std::vector<uint8_t> outPacket(layout.packetSize);
			std::vector<uint8_t> currentPacket(layout.packetSize);
			std::vector<uint8_t> vanillaCopyPacket(layout.packetSize);

			g_clearCharacterSyncPacket(srcPacket.data());
			if (!forceEmptyIncoming) {
				std::memcpy(srcPacket.data() + layout.firstBlockOffset, incomingPacket + 1, incomingPayloadBytes);
			}

			uint8_t playerMask = 0;
			uint8_t peerMask = 0;
			unsigned int playerCountSeen = 0;
			unsigned int activePlayerCount = 0;
			unsigned int peerMatchCount = 0;
			int firstSlotSeen = 0;
			if (session && CallInt0(session, 0x58)) {
				const unsigned int playerCount = static_cast<unsigned int>(CallInt0(session, 0x58));
				playerCountSeen = playerCount;
				for (unsigned int i = 0; i < playerCount; ++i) {
					void* player = CallPtr1UInt(session, 0x60, i);
					if (!player) {
						continue;
					}

					const bool playerActive = CallBool0(player, 0x1C);
					const int slot = static_cast<int>(CallU8_0(player, 0x5C));
					if (!firstSlotSeen && slot > 0) {
						firstSlotSeen = slot;
					}
					if (playerActive) {
						++activePlayerCount;
					}
					if (playerActive && slot > 0 && slot <= 8) {
						playerMask = static_cast<uint8_t>(playerMask | (1u << (slot - 1)));
					}

					if (CallBool1(player, 0x40, peerOrSession) && slot) {
						++peerMatchCount;
						if (slot > 0 && slot <= 8) {
							peerMask = static_cast<uint8_t>(peerMask | (1u << (slot - 1)));
						}
					}
				}
			}
			const bool includeFreshData = (session && CallBool0(session, 0x68)) || ReadU8(self, 0x29A5);
			g_buildCharacterSyncPacket(outPacket.data(), includeFreshData ? 1 : 0, playerMask);

			if (forceEmptyIncoming) {
				void* emptyPacket = g_clearCharacterSyncPacket(currentPacket.data());
				std::memcpy(outPacket.data(), emptyPacket, layout.packetSize);
			}

			g_mergeSelectedBlocks(outPacket.data(), peerMask, srcPacket.data() + layout.firstBlockOffset);
			std::memcpy(currentPacket.data(), outPacket.data(), layout.packetSize);

			if (peerIsHost) {
				if (!ReadU8(self, 0x29A5)) {
					if (!session || !CallBool0(session, 0x68)) {
						void* copied = g_copyVanillaBlocks(vanillaCopyPacket.data(), srcPacket.data() + layout.firstBlockOffset);
						std::memcpy(currentPacket.data(), copied, layout.packetSize);
						if (forceEmptyIncoming) {
							g_clearCharacterTable(*reinterpret_cast<void**>(g_moduleBase + kGlobalLobbyManagerRva));
						}
					}
				}
				g_mergeCharacterSyncData(currentPacket.data(), srcPacket.data() + layout.firstBlockOffset);
				WriteU8(self, 0x29A5, 1);
			} else {
				g_mergeVanillaBlocks(currentPacket.data(), srcPacket.data() + layout.firstBlockOffset);
			}

			g_syncFreshBlocks(currentPacket.data(), outPacket.data() + layout.firstBlockOffset);
			currentPacket[0] = 1;

			const uint8_t applyResult = g_applyCharacterSyncPacket(currentPacket.data());
			static int s_resultLogCount = 0;
			if (s_resultLogCount < 12) {
				++s_resultLogCount;
				Util::Logger::Instance().Get()->info(
					"[NetworkPatch] ReceiveCharacterSync replacement result apply={} force_empty={} include_fresh={} player_mask=0x{:02X} peer_mask=0x{:02X} peer_host={} incoming0=0x{:02X} state=0x{:02X}/0x{:02X} players={} active={} peer_matches={} first_slot={} out_before=0x{:X} buffer=0x{:X} incoming_payload=0x{:X}.",
					static_cast<int>(applyResult),
					static_cast<int>(forceEmptyIncoming),
					includeFreshData,
					playerMask,
					peerMask,
					peerIsHost,
					incomingPacket ? incomingPacket[0] : 0,
					ReadU8(self, 0x29A4),
					ReadU8(self, 0x29A5),
					playerCountSeen,
					activePlayerCount,
					peerMatchCount,
					firstSlotSeen,
					outResult ? outResult[0] : 0,
					static_cast<unsigned int>(layout.packetSize),
					static_cast<unsigned int>(incomingPayloadBytes));
			}

			if (applyResult != 0) {
				SetResult(outResult, 0);
				return outResult;
			}

			*reinterpret_cast<uint32_t*>(g_moduleBase + kGlobalDword360E94Rva) = 13;
			SetResult(outResult, 1);
			return outResult;
		}

		uint32_t* __fastcall DetouredReceiveCharacterSync(void* self, void*, uint32_t* outResult, void* peerOrSession, uint8_t* incomingPacket) {
			const int addonCount = CharacterConfig::Instance().GetAddonCount();
			const ExpandedPacketLayout layout = GetExpandedPacketLayout(addonCount);

			static int s_logCount = 0;
			if (addonCount > 0 && s_logCount < 12) {
				++s_logCount;
				Util::Logger::Instance().Get()->info(
					"[NetworkPatch] ReceiveCharacterSync detour hit addon_count={} replacement={} packet_size=0x{:X} first_bytes=0x{:X} selected=0x{:X} flags=0x{:X}/0x{:X} second=0x{:X} final=0x{:X} tail=0x{:X} peer=0x{:X} incoming=0x{:X}.",
					addonCount,
					kUseReceiveCharacterSyncReplacement,
					layout.packetSize,
					layout.firstBlockBytes,
					layout.selectedBlockOffset,
					layout.firstFlagsOffset,
					layout.selectedFlagsOffset,
					layout.secondBlockOffset,
					layout.finalBlockOffset,
					layout.finalTailOffset,
					reinterpret_cast<uintptr_t>(peerOrSession),
					reinterpret_cast<uintptr_t>(incomingPacket));
			}

			if (!kUseReceiveCharacterSyncReplacement || addonCount <= 0 || !IsPeerCompatibleForExpandedSync(peerOrSession, incomingPacket)) {
				return g_originalReceiveCharacterSync(self, outResult, peerOrSession, incomingPacket);
			}

			return RunReceiveReplacement(self, outResult, peerOrSession, incomingPacket, layout);
		}
	}

	bool SetupReceiveCharacterSyncHook() {
		if (!kInstallReceiveCharacterSyncHook) {
			Util::Logger::Instance().Get()->info("[NetworkPatch] ReceiveCharacterSync hook disabled; using original game network path.");
			return true;
		}

		g_originalReceiveCharacterSync = reinterpret_cast<ReceiveCharacterSyncFn>(g_moduleBase + kReceiveCharacterSyncRva);
		g_clearCharacterSyncPacket = reinterpret_cast<ClearCharacterSyncPacketFn>(g_moduleBase + kClearCharacterSyncPacketRva);
		g_copyVanillaBlocks = reinterpret_cast<CopyVanillaBlocksFn>(g_moduleBase + kCopyVanillaBlocksRva);
		g_syncFreshBlocks = reinterpret_cast<MergePacketFn>(g_moduleBase + kSyncFreshBlocksRva);
		g_mergeVanillaBlocks = reinterpret_cast<MergePacketFn>(g_moduleBase + kMergeVanillaBlocksRva);
		g_mergeCharacterSyncData = reinterpret_cast<MergePacketFn>(g_moduleBase + kMergeCharacterSyncDataRva);
		g_mergeSelectedBlocks = reinterpret_cast<MergeSelectedBlocksFn>(g_moduleBase + kMergeSelectedBlocksRva);
		g_buildCharacterSyncPacket = reinterpret_cast<BuildCharacterSyncPacketFn>(g_moduleBase + kBuildCharacterSyncPacketRva);
		g_applyCharacterSyncPacket = reinterpret_cast<ApplyCharacterSyncPacketFn>(g_moduleBase + kApplyCharacterSyncPacketRva);
		g_clearCharacterTable = reinterpret_cast<ClearCharacterTableFn>(g_moduleBase + kClearCharacterTableRva);
		g_setRemoteCharacterSyncState = reinterpret_cast<SetRemoteCharacterSyncStateFn>(g_moduleBase + kSetRemoteCharacterSyncStateRva);
		g_readTypedValue = reinterpret_cast<ReadTypedValueFn>(g_moduleBase + kReadTypedValueRva);

		Util::Logger::Instance().Get()->info(
			"[Network] Installing hook | name=ReceiveCharacterSync | RVA=0x{:X} | VA=0x{:X} | replacement={}.",
			kReceiveCharacterSyncRva,
			reinterpret_cast<uintptr_t>(g_originalReceiveCharacterSync),
			kUseReceiveCharacterSyncReplacement);

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		LONG attachResult = DetourAttach(&(PVOID&)g_originalReceiveCharacterSync, DetouredReceiveCharacterSync);
		if (attachResult != NO_ERROR) {
			Util::Logger::Instance().Get()->error("[NetworkPatch] ReceiveCharacterSync DetourAttach failed: {}.", attachResult);
			DetourTransactionAbort();
			return false;
		}

		LONG commitResult = DetourTransactionCommit();
		if (commitResult != NO_ERROR) {
			Util::Logger::Instance().Get()->error("[NetworkPatch] ReceiveCharacterSync DetourTransactionCommit failed: {}.", commitResult);
			return false;
		}

		Util::Logger::Instance().Get()->info("[NetworkPatch] ReceiveCharacterSync hook attached successfully.");
		return true;
	}
}