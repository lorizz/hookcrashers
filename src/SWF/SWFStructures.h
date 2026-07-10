#pragma once

#include <cstddef>
#include <cstdint>

namespace HookCrashers::SWF {

constexpr std::uint32_t kFwsSignature = 0x00535746;
constexpr std::uint32_t kCwsSignature = 0x00535743;
constexpr std::uint32_t kZwsSignature = 0x0053575A;

#pragma pack(push, 1)
struct SWFHeader {
    char signature[3];
    std::uint8_t version;
    std::uint32_t fileLength;
};
#pragma pack(pop)

struct SWFBufferView {
    std::uint8_t* base = nullptr;
    SWFHeader* header = nullptr;
    std::uint8_t* rect = nullptr;
    std::uint8_t* frameRate = nullptr;
    std::uint8_t* frameCount = nullptr;
    std::uint8_t* tags = nullptr;
    std::uint32_t fileLength = 0;
    std::uint8_t headerSize = 0;
    std::uint16_t parsedFrameCount = 0;
    std::uint16_t parsedFrameRateRaw = 0;
    bool compressed = false;
    bool valid = false;
};

struct SWFScene {
    std::uint8_t pad000[0x40];
    std::uint8_t pad040[0x80];
    std::uint8_t* tagStream;
    std::uint8_t pad0C4[0x9E];
    std::uint8_t signature;
    std::uint8_t version;
    std::uint32_t fileLength;
    std::uint8_t pad168[0x10];
    std::uint16_t frameCount;
    std::uint8_t headerSize;
    std::uint8_t pad17B[0x0D];
    std::uint8_t* swfBuffer;
};

static_assert(sizeof(SWFHeader) == 8, "SWFHeader size mismatch");
static_assert(offsetof(SWFScene, tagStream) == 0x0C0, "SWFScene::tagStream offset mismatch");
static_assert(offsetof(SWFScene, signature) == 0x162, "SWFScene::signature offset mismatch");
static_assert(offsetof(SWFScene, version) == 0x163, "SWFScene::version offset mismatch");
static_assert(offsetof(SWFScene, fileLength) == 0x164, "SWFScene::fileLength offset mismatch");
static_assert(offsetof(SWFScene, frameCount) == 0x178, "SWFScene::frameCount offset mismatch");
static_assert(offsetof(SWFScene, headerSize) == 0x17A, "SWFScene::headerSize offset mismatch");
static_assert(offsetof(SWFScene, swfBuffer) == 0x188, "SWFScene::swfBuffer offset mismatch");

inline std::uint32_t Signature24(const SWFHeader* header) {
    if (!header) {
        return 0;
    }
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(header->signature[0]))
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(header->signature[1])) << 8)
        | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(header->signature[2])) << 16);
}

inline bool IsKnownSWFSignature(const SWFHeader* header) {
    const std::uint32_t signature = Signature24(header);
    return signature == kFwsSignature || signature == kCwsSignature || signature == kZwsSignature;
}

inline std::uint8_t ReadRectEncodedByteCount(const std::uint8_t* rect) {
    if (!rect) {
        return 0;
    }
    const std::uint8_t nbits = rect[0] >> 3;
    const std::uint16_t totalBits = static_cast<std::uint16_t>(5 + nbits * 4);
    return static_cast<std::uint8_t>((totalBits + 7) / 8);
}

inline SWFBufferView MakeSWFBufferView(std::uint8_t* swfBuffer) {
    SWFBufferView view;
    view.base = swfBuffer;
    view.header = reinterpret_cast<SWFHeader*>(swfBuffer);
    if (!swfBuffer || !IsKnownSWFSignature(view.header)) {
        return view;
    }

    view.valid = true;
    view.compressed = view.header->signature[0] == 'C' || view.header->signature[0] == 'Z';
    view.fileLength = view.header->fileLength;
    view.rect = swfBuffer + sizeof(SWFHeader);
    const std::uint8_t rectBytes = ReadRectEncodedByteCount(view.rect);
    view.frameRate = view.rect + rectBytes;
    view.frameCount = view.frameRate + 2;
    view.tags = view.frameCount + 2;
    view.headerSize = static_cast<std::uint8_t>(view.tags - swfBuffer);
    view.parsedFrameRateRaw = *reinterpret_cast<std::uint16_t*>(view.frameRate);
    view.parsedFrameCount = *reinterpret_cast<std::uint16_t*>(view.frameCount);
    return view;
}

} // namespace HookCrashers::SWF
