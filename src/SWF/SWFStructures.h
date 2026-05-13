#pragma once

#include <cstdint>
#include <cstddef>

namespace HookCrashers::SWF {
	constexpr uint32_t kFwsSignature = 0x00535746; // "FWS" little-endian in the low 24 bits.
	constexpr uint32_t kCwsSignature = 0x00535743; // "CWS".
	constexpr uint32_t kZwsSignature = 0x0053575A; // "ZWS".

#pragma pack(push, 1)
	struct SWFHeader {
		char signature[3];
		uint8_t version;
		uint32_t fileLength;
	};
#pragma pack(pop)

	struct SWFRectInfo {
		uint8_t encodedByteCount = 0;
	};

	struct SWFBufferView {
		uint8_t* base = nullptr;
		SWFHeader* header = nullptr;
		uint8_t* rect = nullptr;
		uint8_t* frameRate = nullptr;
		uint8_t* frameCount = nullptr;
		uint8_t* tags = nullptr;
		uint32_t fileLength = 0;
		uint8_t headerSize = 0;
		uint16_t parsedFrameCount = 0;
		uint16_t parsedFrameRateRaw = 0;
		bool compressed = false;
		bool valid = false;
	};

	struct SWFScene {
		uint8_t pad000[0x40];
		// 0x40..0x4B are used by parsed movie bounds/twips in ParseHeader.
		uint8_t pad040[0x80];
		uint8_t* tagStream;       // 0x0C0: first tag after SWF header.
		uint8_t pad0C4[0x9E];
		uint8_t signature;        // 0x162: original SWF signature byte, 'F' for uncompressed.
		uint8_t version;          // 0x163.
		uint32_t fileLength;      // 0x164.
		uint8_t pad168[0x10];
		uint16_t frameCount;      // 0x178.
		uint8_t headerSize;       // 0x17A.
		uint8_t pad17B[0x0D];
		uint8_t* swfBuffer;       // 0x188.
	};

	static_assert(sizeof(SWFHeader) == 8, "SWFHeader layout must match the SWF file header.");
	static_assert(offsetof(SWFScene, tagStream) == 0x0C0, "SWFScene::tagStream offset mismatch.");
	static_assert(offsetof(SWFScene, signature) == 0x162, "SWFScene::signature offset mismatch.");
	static_assert(offsetof(SWFScene, version) == 0x163, "SWFScene::version offset mismatch.");
	static_assert(offsetof(SWFScene, fileLength) == 0x164, "SWFScene::fileLength offset mismatch.");
	static_assert(offsetof(SWFScene, frameCount) == 0x178, "SWFScene::frameCount offset mismatch.");
	static_assert(offsetof(SWFScene, headerSize) == 0x17A, "SWFScene::headerSize offset mismatch.");
	static_assert(offsetof(SWFScene, swfBuffer) == 0x188, "SWFScene::swfBuffer offset mismatch.");

	inline uint32_t Signature24(const SWFHeader* header) {
		if (!header) {
			return 0;
		}
		return static_cast<uint32_t>(static_cast<uint8_t>(header->signature[0]))
			| (static_cast<uint32_t>(static_cast<uint8_t>(header->signature[1])) << 8)
			| (static_cast<uint32_t>(static_cast<uint8_t>(header->signature[2])) << 16);
	}

	inline bool IsKnownSWFSignature(const SWFHeader* header) {
		const uint32_t signature = Signature24(header);
		return signature == kFwsSignature || signature == kCwsSignature || signature == kZwsSignature;
	}

	inline uint8_t ReadRectEncodedByteCount(const uint8_t* rect) {
		if (!rect) {
			return 0;
		}
		const uint8_t nbits = rect[0] >> 3;
		const uint16_t totalBits = 5 + static_cast<uint16_t>(nbits) * 4;
		return static_cast<uint8_t>((totalBits + 7) / 8);
	}

	inline SWFBufferView MakeSWFBufferView(uint8_t* swfBuffer) {
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
		const uint8_t rectBytes = ReadRectEncodedByteCount(view.rect);
		view.frameRate = view.rect + rectBytes;
		view.frameCount = view.frameRate + 2;
		view.tags = view.frameCount + 2;
		view.headerSize = static_cast<uint8_t>(view.tags - swfBuffer);
		view.parsedFrameRateRaw = *reinterpret_cast<uint16_t*>(view.frameRate);
		view.parsedFrameCount = *reinterpret_cast<uint16_t*>(view.frameCount);
		return view;
	}
}
