#pragma once

// Castle Crashers save encryption, platform independent (no game calls).
//
// cc_save.dat is Blowfish-ECB over 8-byte blocks with little-endian data words.
// The 24-byte key is a constant from .rdata with the owner's SteamID64 written
// into it; the last dword of the plaintext is a 16-bit-LCG checksum of the rest.
// The Blowfish init tables (P + S boxes) are read from the game image at runtime
// and from the unpacked executable in tests, so they are not duplicated here.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace HookCrashers::Save::Crypto {

constexpr std::size_t kInitTableWords = 18 + 4 * 256;  // P array followed by four S boxes
constexpr std::size_t kKeySize = 0x18;
constexpr std::size_t kBlockSize = 8;
constexpr std::size_t kVersionOffset = 5;
constexpr std::uint8_t kCurrentSaveVersion = 2;

using Key = std::array<std::uint8_t, kKeySize>;

// Native key init FUN_001e8de0: SteamID64 halves stored as little-endian u16s.
Key DeriveKey(const std::uint8_t* baseKey, std::uint64_t steamId);

// Whole 8-byte blocks only; a trailing partial block is left as is (the game does the same).
void Decrypt(std::vector<std::uint8_t>& data, const std::uint32_t* initTables, const Key& key);
void Encrypt(std::vector<std::uint8_t>& data, const std::uint32_t* initTables, const Key& key);

// Checksum of data[0 .. size-4) (native 0x130EE0).
std::uint32_t Checksum(const std::uint8_t* data, std::size_t size);
bool HasValidChecksum(const std::vector<std::uint8_t>& plain);
void StoreChecksum(std::vector<std::uint8_t>& plain);

} // namespace HookCrashers::Save::Crypto
