#include "SaveCrypto.h"

#include <cstring>
#include <memory>
#include <utility>

namespace HookCrashers::Save::Crypto {
namespace {

class Blowfish {
public:
    Blowfish(const std::uint32_t* initTables, const Key& key) {
        std::memcpy(m_p, initTables, sizeof(m_p));
        std::memcpy(m_s, initTables + 18, sizeof(m_s));
        std::size_t cursor = 0;
        for (std::uint32_t& word : m_p) {
            std::uint32_t value = 0;
            for (int i = 0; i < 4; ++i) {
                value = (value << 8) | key[cursor++ % key.size()];
            }
            word ^= value;
        }
        std::uint32_t left = 0;
        std::uint32_t right = 0;
        for (std::size_t i = 0; i < 18; i += 2) {
            EncryptBlock(left, right);
            m_p[i] = left;
            m_p[i + 1] = right;
        }
        for (auto& box : m_s) {
            for (std::size_t i = 0; i < 256; i += 2) {
                EncryptBlock(left, right);
                box[i] = left;
                box[i + 1] = right;
            }
        }
    }

    void EncryptBlock(std::uint32_t& left, std::uint32_t& right) const {
        for (int i = 0; i < 16; ++i) {
            left ^= m_p[i];
            right ^= F(left);
            std::swap(left, right);
        }
        std::swap(left, right);
        right ^= m_p[16];
        left ^= m_p[17];
    }

    void DecryptBlock(std::uint32_t& left, std::uint32_t& right) const {
        for (int i = 17; i > 1; --i) {
            left ^= m_p[i];
            right ^= F(left);
            std::swap(left, right);
        }
        std::swap(left, right);
        right ^= m_p[1];
        left ^= m_p[0];
    }

private:
    std::uint32_t F(std::uint32_t x) const {
        return ((m_s[0][x >> 24] + m_s[1][(x >> 16) & 0xFF]) ^ m_s[2][(x >> 8) & 0xFF]) + m_s[3][x & 0xFF];
    }

    std::uint32_t m_p[18];
    std::uint32_t m_s[4][256];
};

std::uint32_t LoadLe(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

void StoreLe(std::uint8_t* bytes, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        bytes[i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

void StoreLe16(std::uint8_t* bytes, std::uint32_t value) {
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8);
}

template <bool kEncrypt>
void Transform(std::vector<std::uint8_t>& data, const std::uint32_t* initTables, const Key& key) {
    // ~4 KiB of state: keep it off the (possibly small) game-thread stack.
    const auto cipher = std::make_unique<Blowfish>(initTables, key);
    for (std::size_t offset = 0; offset + kBlockSize <= data.size(); offset += kBlockSize) {
        std::uint32_t left = LoadLe(&data[offset]);
        std::uint32_t right = LoadLe(&data[offset + 4]);
        if (kEncrypt) {
            cipher->EncryptBlock(left, right);
        } else {
            cipher->DecryptBlock(left, right);
        }
        StoreLe(&data[offset], left);
        StoreLe(&data[offset + 4], right);
    }
}

} // namespace

Key DeriveKey(const std::uint8_t* baseKey, std::uint64_t steamId) {
    Key key {};
    std::memcpy(key.data(), baseKey, key.size());
    const auto high = static_cast<std::uint32_t>(steamId >> 32);
    const auto low = static_cast<std::uint32_t>(steamId);
    StoreLe16(&key[0x00], high >> 16);
    StoreLe16(&key[0x13], high & 0xFFFF);
    StoreLe16(&key[0x05], low >> 16);
    StoreLe16(&key[0x0F], low & 0xFFFF);
    return key;
}

void Decrypt(std::vector<std::uint8_t>& data, const std::uint32_t* initTables, const Key& key) {
    Transform<false>(data, initTables, key);
}

void Encrypt(std::vector<std::uint8_t>& data, const std::uint32_t* initTables, const Key& key) {
    Transform<true>(data, initTables, key);
}

std::uint32_t Checksum(const std::uint8_t* data, std::size_t size) {
    if (size < 4) {
        return 0;
    }
    const std::size_t length = size - 4;
    std::uint32_t state = 0xD971;
    std::uint32_t even = 0;
    std::uint32_t odd = 0;
    std::size_t i = 0;
    for (; i + 1 < length; i += 2) {
        const std::uint32_t x = ((state >> 8) ^ data[i]) & 0xFF;
        const std::uint32_t s = ((x + state) * 0xFFFFCE6Du + 0x58BF) & 0xFFFF;
        even += x;
        const std::uint32_t y = ((s >> 8) ^ data[i + 1]) & 0xFF;
        state = ((y + s) * 0xFFFFCE6Du + 0x58BF) & 0xFFFF;
        odd += y;
    }
    const std::uint32_t tail = (length % 2) ? (((state >> 8) ^ data[length - 1]) & 0xFF) : 0;
    return even + odd + tail;
}

bool HasValidChecksum(const std::vector<std::uint8_t>& plain) {
    return plain.size() >= 8 && Checksum(plain.data(), plain.size()) == LoadLe(&plain[plain.size() - 4]);
}

void StoreChecksum(std::vector<std::uint8_t>& plain) {
    if (plain.size() >= 4) {
        StoreLe(&plain[plain.size() - 4], Checksum(plain.data(), plain.size()));
    }
}

} // namespace HookCrashers::Save::Crypto
