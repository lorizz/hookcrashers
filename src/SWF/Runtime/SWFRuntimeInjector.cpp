#include "SWFRuntimeInjector.h"

#include "../../Save/CharacterConfig.h"
#include "../../Util/Logger.h"
#include "../SWFBytecode.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <objbase.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#pragma comment(lib, "windowscodecs.lib")

namespace HookCrashers::SWF::Runtime {
	namespace {
		struct Tag {
			uint16_t code = 0;
			const uint8_t* begin = nullptr;
			const uint8_t* payload = nullptr;
			const uint8_t* next = nullptr;
			uint32_t length = 0;
		};

		struct ImageRgba {
			uint16_t width = 0;
			uint16_t height = 0;
			std::vector<uint8_t> rgba;
		};

		struct ColorRgba {
			uint8_t r = 0;
			uint8_t g = 0;
			uint8_t b = 0;
			uint8_t a = 0;
		};

		struct SvgPoint {
			float x = 0.0f;
			float y = 0.0f;
		};

		struct SvgTransform {
			float a = 1.0f;
			float b = 0.0f;
			float c = 0.0f;
			float d = 1.0f;
			float e = 0.0f;
			float f = 0.0f;
		};

		struct SvgShape {
			ColorRgba fill;
			std::vector<SvgPoint> points;
		};

		struct SvgDocument {
			float width = 0.0f;
			float height = 0.0f;
			float viewMinX = 0.0f;
			float viewMinY = 0.0f;
			float viewWidth = 0.0f;
			float viewHeight = 0.0f;
			std::vector<SvgShape> shapes;
		};

		struct PortraitDefinition {
			std::string characterId;
			std::string sourcePath;
			uint16_t bitmapId = 0;
			uint16_t shapeId = 0;
			ImageRgba image;
			SvgDocument svg;
			bool isSvg = false;
		};

		struct CharacterPortraitPlan {
			std::string characterId;
			bool freshOnly = false;
			std::string classicPath;
			std::string freshPath;
		};

		struct PortraitPlacement {
			uint16_t shapeId = 0;
		};

		struct RectI32 {
			int32_t xmin = 0;
			int32_t xmax = 0;
			int32_t ymin = 0;
			int32_t ymax = 0;
		};

		struct VectorRun {
			uint16_t fillIndex = 0;
			int32_t x1 = 0;
			int32_t x2 = 0;
			int32_t y1 = 0;
			int32_t y2 = 0;
		};

		struct InjectionSet {
			std::vector<PortraitDefinition> definitions;
			std::vector<PortraitPlacement> classicPlacements;
			std::vector<PortraitPlacement> freshPlacements;
			std::vector<uint8_t> shapeTemplate152;
			RectI32 shapeTemplate152Bounds;
			std::vector<uint8_t> shapeTemplate152BoundsBytes;
		};

		std::vector<std::unique_ptr<std::vector<uint8_t>>> g_ownedPatchedBuffers;

		bool PathContainsLobby(const std::string& path) {
			std::string lower = path;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			return lower.find("lobby") != std::string::npos;
		}

		bool FileExistsA(const std::string& path) {
			return !path.empty() && GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
		}

		std::wstring WidenUtf8(const std::string& value) {
			if (value.empty()) {
				return {};
			}
			const int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
			if (size <= 1) {
				const int ansiSize = MultiByteToWideChar(CP_ACP, 0, value.c_str(), -1, nullptr, 0);
				std::wstring ansi(static_cast<size_t>(std::max(0, ansiSize)), L'\0');
				if (ansiSize > 1) {
					MultiByteToWideChar(CP_ACP, 0, value.c_str(), -1, ansi.data(), ansiSize);
					if (!ansi.empty() && ansi.back() == L'\0') {
						ansi.pop_back();
					}
				}
				return ansi;
			}
			std::wstring out(static_cast<size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), size);
			if (!out.empty() && out.back() == L'\0') {
				out.pop_back();
			}
			return out;
		}

		uint16_t ReadU16(const uint8_t* p) {
			return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8);
		}

		uint32_t ReadU32(const uint8_t* p) {
			return static_cast<uint32_t>(p[0])
				| (static_cast<uint32_t>(p[1]) << 8)
				| (static_cast<uint32_t>(p[2]) << 16)
				| (static_cast<uint32_t>(p[3]) << 24);
		}

		void WriteU16(std::vector<uint8_t>& out, uint16_t value) {
			out.push_back(static_cast<uint8_t>(value & 0xFF));
			out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
		}

		void WriteU32(std::vector<uint8_t>& out, uint32_t value) {
			out.push_back(static_cast<uint8_t>(value & 0xFF));
			out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
			out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
			out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
		}

		void PatchU16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
			if (offset + 1 >= bytes.size()) {
				return;
			}
			bytes[offset] = static_cast<uint8_t>(value & 0xFF);
			bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
		}

		bool ReadTag(const uint8_t* cursor, const uint8_t* end, Tag& tag) {
			if (!cursor || cursor + 2 > end) {
				return false;
			}
			const uint8_t* start = cursor;
			const uint16_t packed = ReadU16(cursor);
			cursor += 2;
			tag.code = packed >> 6;
			tag.length = packed & 0x3F;
			if (tag.length == 0x3F) {
				if (cursor + 4 > end) {
					return false;
				}
				tag.length = ReadU32(cursor);
				cursor += 4;
			}
			if (cursor + tag.length > end) {
				return false;
			}
			tag.begin = start;
			tag.payload = cursor;
			tag.next = cursor + tag.length;
			return true;
		}

		void AppendTag(std::vector<uint8_t>& out, uint16_t code, const std::vector<uint8_t>& payload) {
			const uint32_t length = static_cast<uint32_t>(payload.size());
			WriteU16(out, static_cast<uint16_t>((code << 6) | (length >= 0x3F ? 0x3F : length)));
			if (length >= 0x3F) {
				WriteU32(out, length);
			}
			out.insert(out.end(), payload.begin(), payload.end());
		}

		void AppendTag(std::vector<uint8_t>& out, uint16_t code, const uint8_t* payload, size_t length) {
			std::vector<uint8_t> temp(payload, payload + length);
			AppendTag(out, code, temp);
		}

		void AppendTagLong(std::vector<uint8_t>& out, uint16_t code, const std::vector<uint8_t>& payload) {
			WriteU16(out, static_cast<uint16_t>((code << 6) | 0x3F));
			WriteU32(out, static_cast<uint32_t>(payload.size()));
			out.insert(out.end(), payload.begin(), payload.end());
		}

		uint32_t Adler32(const std::vector<uint8_t>& data) {
			uint32_t a = 1;
			uint32_t b = 0;
			for (uint8_t value : data) {
				a = (a + value) % 65521u;
				b = (b + a) % 65521u;
			}
			return (b << 16) | a;
		}

		std::vector<uint8_t> ZlibStore(const std::vector<uint8_t>& data) {
			std::vector<uint8_t> out;
			out.reserve(data.size() + data.size() / 65535u * 5u + 16u);
			out.push_back(0x78);
			out.push_back(0x01);

			size_t offset = 0;
			while (offset < data.size() || data.empty()) {
				const size_t remaining = data.size() - offset;
				const uint16_t blockLen = static_cast<uint16_t>(std::min(remaining, static_cast<size_t>(65535)));
				const bool finalBlock = offset + blockLen >= data.size();
				out.push_back(finalBlock ? 0x01 : 0x00);
				WriteU16(out, blockLen);
				WriteU16(out, static_cast<uint16_t>(~blockLen));
				out.insert(out.end(), data.begin() + offset, data.begin() + offset + blockLen);
				offset += blockLen;
				if (data.empty()) {
					break;
				}
			}

			WriteU32(out, Adler32(data));
			std::reverse(out.end() - 4, out.end());
			return out;
		}

		uint16_t ReverseBits(uint16_t value, int count) {
			uint16_t result = 0;
			for (int i = 0; i < count; ++i) {
				result = static_cast<uint16_t>((result << 1) | (value & 1));
				value >>= 1;
			}
			return result;
		}

		struct LsbBitWriter {
			std::vector<uint8_t> bytes;
			uint32_t bitBuffer = 0;
			int bitCount = 0;

			void Write(uint32_t value, int count) {
				bitBuffer |= value << bitCount;
				bitCount += count;
				while (bitCount >= 8) {
					bytes.push_back(static_cast<uint8_t>(bitBuffer & 0xFF));
					bitBuffer >>= 8;
					bitCount -= 8;
				}
			}

			void Flush() {
				if (bitCount > 0) {
					bytes.push_back(static_cast<uint8_t>(bitBuffer & 0xFF));
					bitBuffer = 0;
					bitCount = 0;
				}
			}
		};

		void WriteFixedSymbol(LsbBitWriter& writer, uint16_t symbol) {
			if (symbol <= 143) {
				writer.Write(ReverseBits(static_cast<uint16_t>(0x30 + symbol), 8), 8);
			}
			else if (symbol <= 255) {
				writer.Write(ReverseBits(static_cast<uint16_t>(0x190 + symbol - 144), 9), 9);
			}
			else if (symbol <= 279) {
				writer.Write(ReverseBits(static_cast<uint16_t>(symbol - 256), 7), 7);
			}
			else {
				writer.Write(ReverseBits(static_cast<uint16_t>(0xC0 + symbol - 280), 8), 8);
			}
		}

		std::vector<uint8_t> ZlibDeflateFixed(const std::vector<uint8_t>& data) {
			static constexpr uint16_t kLengthBase[29] = {
				3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
				31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
			};
			static constexpr uint8_t kLengthExtra[29] = {
				0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
				2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
			};
			static constexpr uint16_t kDistanceBase[30] = {
				1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
				193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
				6145, 8193, 12289, 16385, 24577
			};
			static constexpr uint8_t kDistanceExtra[30] = {
				0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
				6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
			};

			auto lengthCode = [&](size_t length, uint16_t& code, uint16_t& extraValue, uint8_t& extraBits) {
				for (uint16_t i = 0; i < 29; ++i) {
					const uint16_t base = kLengthBase[i];
					const uint16_t maxValue = static_cast<uint16_t>(base + ((1u << kLengthExtra[i]) - 1u));
					if (length >= base && length <= maxValue) {
						code = static_cast<uint16_t>(257 + i);
						extraValue = static_cast<uint16_t>(length - base);
						extraBits = kLengthExtra[i];
						return;
					}
				}
				code = 285;
				extraValue = 0;
				extraBits = 0;
				};

			auto distanceCode = [&](size_t distance, uint16_t& code, uint16_t& extraValue, uint8_t& extraBits) {
				for (uint16_t i = 0; i < 30; ++i) {
					const uint16_t base = kDistanceBase[i];
					const uint16_t maxValue = static_cast<uint16_t>(base + ((1u << kDistanceExtra[i]) - 1u));
					if (distance >= base && distance <= maxValue) {
						code = i;
						extraValue = static_cast<uint16_t>(distance - base);
						extraBits = kDistanceExtra[i];
						return;
					}
				}
				code = 29;
				extraValue = static_cast<uint16_t>(distance - kDistanceBase[29]);
				extraBits = kDistanceExtra[29];
				};

			std::vector<int32_t> lastSeen(65536, -1);
			LsbBitWriter writer;
			writer.Write(1, 1);
			writer.Write(1, 2);

			size_t pos = 0;
			while (pos < data.size()) {
				size_t bestLength = 0;
				size_t bestDistance = 0;
				if (pos + 3 <= data.size()) {
					const uint32_t hash = (static_cast<uint32_t>(data[pos]) * 251u
						+ static_cast<uint32_t>(data[pos + 1]) * 31u
						+ static_cast<uint32_t>(data[pos + 2])) & 0xFFFFu;
					const int32_t candidate = lastSeen[hash];
					if (candidate >= 0 && pos - static_cast<size_t>(candidate) <= 32768u) {
						size_t length = 0;
						const size_t maxLength = std::min<size_t>(258, data.size() - pos);
						while (length < maxLength && data[static_cast<size_t>(candidate) + length] == data[pos + length]) {
							++length;
						}
						if (length >= 3) {
							bestLength = length;
							bestDistance = pos - static_cast<size_t>(candidate);
						}
					}
					lastSeen[hash] = static_cast<int32_t>(pos);
				}

				if (bestLength >= 3) {
					uint16_t code = 0;
					uint16_t extraValue = 0;
					uint8_t extraBits = 0;
					lengthCode(bestLength, code, extraValue, extraBits);
					WriteFixedSymbol(writer, code);
					if (extraBits) {
						writer.Write(extraValue, extraBits);
					}
					distanceCode(bestDistance, code, extraValue, extraBits);
					writer.Write(ReverseBits(code, 5), 5);
					if (extraBits) {
						writer.Write(extraValue, extraBits);
					}
					for (size_t i = 1; i < bestLength && pos + i + 2 < data.size(); ++i) {
						const uint32_t hash = (static_cast<uint32_t>(data[pos + i]) * 251u
							+ static_cast<uint32_t>(data[pos + i + 1]) * 31u
							+ static_cast<uint32_t>(data[pos + i + 2])) & 0xFFFFu;
						lastSeen[hash] = static_cast<int32_t>(pos + i);
					}
					pos += bestLength;
				}
				else {
					WriteFixedSymbol(writer, data[pos]);
					++pos;
				}
			}

			WriteFixedSymbol(writer, 256);
			writer.Flush();

			std::vector<uint8_t> out;
			out.reserve(writer.bytes.size() + 6);
			out.push_back(0x78);
			out.push_back(0x01);
			out.insert(out.end(), writer.bytes.begin(), writer.bytes.end());
			WriteU32(out, Adler32(data));
			std::reverse(out.end() - 4, out.end());
			return out;
		}

		int SignedBitCount(const std::vector<int32_t>& values) {
			int32_t maxAbs = 0;
			for (int32_t value : values) {
				maxAbs = std::max<int32_t>(maxAbs, value < 0 ? -value : value);
			}
			int bits = 1;
			while (maxAbs >= (1 << (bits - 1))) {
				++bits;
			}
			return bits;
		}

		void PutBits(std::vector<uint8_t>& bits, uint32_t value, int count) {
			for (int shift = count - 1; shift >= 0; --shift) {
				bits.push_back(static_cast<uint8_t>((value >> shift) & 1));
			}
		}

		void PutSignedBits(std::vector<uint8_t>& bits, int32_t value, int count) {
			uint32_t encoded = value < 0
				? static_cast<uint32_t>((1ll << count) + value)
				: static_cast<uint32_t>(value);
			PutBits(bits, encoded, count);
		}

		uint32_t ReadPackedBits(const uint8_t* data, size_t bitOffset, int count) {
			uint32_t value = 0;
			for (int i = 0; i < count; ++i) {
				const size_t bit = bitOffset + static_cast<size_t>(i);
				const uint8_t byte = data[bit / 8u];
				const uint8_t mask = static_cast<uint8_t>(0x80u >> (bit % 8u));
				value = (value << 1) | ((byte & mask) ? 1u : 0u);
			}
			return value;
		}

		int32_t ReadPackedSignedBits(const uint8_t* data, size_t& bitOffset, int count) {
			const uint32_t raw = ReadPackedBits(data, bitOffset, count);
			bitOffset += static_cast<size_t>(count);
			const uint32_t sign = 1u << (count - 1);
			if ((raw & sign) == 0) {
				return static_cast<int32_t>(raw);
			}
			return static_cast<int32_t>(raw | (~0u << count));
		}

		bool DecodeRect(const uint8_t* data, size_t length, RectI32& rect, size_t& byteLength) {
			if (!data || length == 0) {
				return false;
			}
			const int nbits = static_cast<int>(ReadPackedBits(data, 0, 5));
			const size_t totalBits = 5u + 4u * static_cast<size_t>(nbits);
			byteLength = (totalBits + 7u) / 8u;
			if (nbits <= 0 || byteLength > length) {
				return false;
			}
			size_t bitOffset = 5;
			rect.xmin = ReadPackedSignedBits(data, bitOffset, nbits);
			rect.xmax = ReadPackedSignedBits(data, bitOffset, nbits);
			rect.ymin = ReadPackedSignedBits(data, bitOffset, nbits);
			rect.ymax = ReadPackedSignedBits(data, bitOffset, nbits);
			return true;
		}

		std::vector<uint8_t> PackBits(const std::vector<uint8_t>& bits) {
			std::vector<uint8_t> out;
			uint8_t cur = 0;
			for (size_t i = 0; i < bits.size(); ++i) {
				cur = static_cast<uint8_t>((cur << 1) | (bits[i] & 1));
				if ((i + 1) % 8 == 0) {
					out.push_back(cur);
					cur = 0;
				}
			}
			const size_t rem = bits.size() % 8;
			if (rem != 0) {
				out.push_back(static_cast<uint8_t>(cur << (8 - rem)));
			}
			return out;
		}

		std::vector<uint8_t> EncodeRect(int32_t xmin, int32_t xmax, int32_t ymin, int32_t ymax) {
			std::vector<uint8_t> bits;
			const int n = SignedBitCount({ xmin, xmax, ymin, ymax });
			PutBits(bits, static_cast<uint32_t>(n), 5);
			PutSignedBits(bits, xmin, n);
			PutSignedBits(bits, xmax, n);
			PutSignedBits(bits, ymin, n);
			PutSignedBits(bits, ymax, n);
			return PackBits(bits);
		}

		std::vector<uint8_t> EncodeMatrix(float scaleX, float scaleY) {
			std::vector<uint8_t> bits;
			PutBits(bits, 1, 1);
			const int sx = static_cast<int>(scaleX * 65536.0f + 0.5f);
			const int sy = static_cast<int>(scaleY * 65536.0f + 0.5f);
			const int scaleBits = SignedBitCount({ sx, sy });
			PutBits(bits, static_cast<uint32_t>(scaleBits), 5);
			PutSignedBits(bits, sx, scaleBits);
			PutSignedBits(bits, sy, scaleBits);
			PutBits(bits, 0, 1);
			PutBits(bits, 1, 5);
			PutSignedBits(bits, 0, 1);
			PutSignedBits(bits, 0, 1);
			return PackBits(bits);
		}

		void PutStraightEdge(std::vector<uint8_t>& bits, int32_t dx, int32_t dy) {
			PutBits(bits, 1, 1);
			PutBits(bits, 1, 1);
			const int n = std::max(2, SignedBitCount({ dx, dy }));
			PutBits(bits, static_cast<uint32_t>(n - 2), 4);
			PutBits(bits, 1, 1);
			PutSignedBits(bits, dx, n);
			PutSignedBits(bits, dy, n);
		}

		std::vector<uint8_t> EncodePlaceObject2(uint16_t characterId, uint16_t depth) {
			std::vector<uint8_t> out;
			out.push_back(0x03);
			WriteU16(out, depth);
			WriteU16(out, characterId);
			return out;
		}

		std::vector<uint8_t> EncodeDefineBitsLossless2(const PortraitDefinition& portrait) {
			std::vector<uint8_t> bitmap;
			bitmap.reserve(static_cast<size_t>(portrait.image.width) * portrait.image.height * 4u);
			for (size_t pixel = 0; pixel + 3 < portrait.image.rgba.size(); pixel += 4) {
				bitmap.push_back(portrait.image.rgba[pixel + 3]);
				bitmap.push_back(portrait.image.rgba[pixel + 0]);
				bitmap.push_back(portrait.image.rgba[pixel + 1]);
				bitmap.push_back(portrait.image.rgba[pixel + 2]);
			}

			std::vector<uint8_t> out;
			WriteU16(out, portrait.bitmapId);
			out.push_back(5);
			WriteU16(out, portrait.image.width);
			WriteU16(out, portrait.image.height);
			const std::vector<uint8_t> compressed = ZlibDeflateFixed(bitmap);
			out.insert(out.end(), compressed.begin(), compressed.end());
			Util::Logger::Instance().Get()->info(
				"[SWFInject] compressed lossless portrait source='{}' bitmap_id={} png={}x{} raw_argb_bytes={} zlib_bytes={}.",
				portrait.sourcePath,
				portrait.bitmapId,
				portrait.image.width,
				portrait.image.height,
				bitmap.size(),
				compressed.size());
			return out;
		}

		std::vector<uint8_t> EncodeAttachSkinGraphicAction() {
			std::vector<uint8_t> pool;
			WriteU16(pool, 2);
			pool.insert(pool.end(), { 't','h','i','s',0 });
			pool.insert(pool.end(), { 'o','u','t','s','i','d','e','_','s','w','f',0 });

			std::vector<uint8_t> push;
			push.push_back(7); WriteU32(push, 36);
			push.push_back(8); push.push_back(0);
			push.push_back(7); WriteU32(push, 2);
			push.push_back(7); WriteU32(push, 0xE2);

			std::vector<uint8_t> out;
			out.push_back(0x88); WriteU16(out, static_cast<uint16_t>(pool.size())); out.insert(out.end(), pool.begin(), pool.end());
			out.push_back(0x96); WriteU16(out, static_cast<uint16_t>(push.size())); out.insert(out.end(), push.begin(), push.end());
			out.push_back(0x3D);
			out.push_back(0x17);
			out.push_back(0x07);
			out.push_back(0x00);
			return out;
		}

		int BitsNeeded(uint32_t value) {
			int bits = 0;
			do {
				++bits;
				value >>= 1;
			} while (value);
			return bits;
		}

		uint32_t ColorKey(const uint8_t* rgba) {
			const uint8_t r = static_cast<uint8_t>(rgba[0] & 0xF0);
			const uint8_t g = static_cast<uint8_t>(rgba[1] & 0xF0);
			const uint8_t b = static_cast<uint8_t>(rgba[2] & 0xF0);
			const uint8_t a = rgba[3] < 16 ? 0 : static_cast<uint8_t>(rgba[3] & 0xF0);
			return static_cast<uint32_t>(r)
				| (static_cast<uint32_t>(g) << 8)
				| (static_cast<uint32_t>(b) << 16)
				| (static_cast<uint32_t>(a) << 24);
		}

		void PutStyleChangeMoveFill(std::vector<uint8_t>& bits, int32_t x, int32_t y, uint16_t fillIndex, int fillBits) {
			PutBits(bits, 0, 1);
			PutBits(bits, 0, 1);
			PutBits(bits, 0, 1);
			PutBits(bits, 1, 1);
			PutBits(bits, 1, 1);
			PutBits(bits, 1, 1);
			const int moveBits = SignedBitCount({ x, y });
			PutBits(bits, static_cast<uint32_t>(moveBits), 5);
			PutSignedBits(bits, x, moveBits);
			PutSignedBits(bits, y, moveBits);
			PutBits(bits, fillIndex, fillBits);
			PutBits(bits, 0, fillBits);
		}

		std::vector<uint8_t> EncodeVectorShapeFromImage(const PortraitDefinition& portrait, const RectI32& bounds, const std::vector<uint8_t>& boundsBytes) {
			const int32_t widthTwips = std::max<int32_t>(1, bounds.xmax - bounds.xmin);
			const int32_t heightTwips = std::max<int32_t>(1, bounds.ymax - bounds.ymin);
			const uint16_t sourceW = portrait.image.width;
			const uint16_t sourceH = portrait.image.height;
			const uint16_t workW = std::min<uint16_t>(sourceW, 64);
			const uint16_t workH = std::min<uint16_t>(sourceH, 64);
			std::vector<ColorRgba> fills;
			std::vector<VectorRun> runs;
			fills.reserve(256);
			runs.reserve(static_cast<size_t>(workW) * workH / 4u);

			auto fillForColor = [&](uint32_t key) -> uint16_t {
				const ColorRgba color{
					static_cast<uint8_t>(key & 0xFF),
					static_cast<uint8_t>((key >> 8) & 0xFF),
					static_cast<uint8_t>((key >> 16) & 0xFF),
					static_cast<uint8_t>((key >> 24) & 0xFF)
				};
				for (size_t i = 0; i < fills.size(); ++i) {
					const ColorRgba& existing = fills[i];
					if (existing.r == color.r && existing.g == color.g && existing.b == color.b && existing.a == color.a) {
						return static_cast<uint16_t>(i + 1);
					}
				}
				if (fills.size() >= 65534) {
					return 0;
				}
				fills.push_back(color);
				return static_cast<uint16_t>(fills.size());
				};

			auto sampleKey = [&](uint16_t x, uint16_t y) -> uint32_t {
				const uint16_t sx = static_cast<uint16_t>((static_cast<uint32_t>(x) * sourceW) / workW);
				const uint16_t sy = static_cast<uint16_t>((static_cast<uint32_t>(y) * sourceH) / workH);
				const size_t pixel = (static_cast<size_t>(sy) * sourceW + sx) * 4u;
				return ColorKey(&portrait.image.rgba[pixel]);
				};

			struct ActiveRun {
				uint32_t key = 0;
				uint16_t fillIndex = 0;
				uint16_t x1 = 0;
				uint16_t x2 = 0;
				uint16_t y1 = 0;
				uint16_t y2 = 0;
				bool extended = false;
			};

			auto flushRun = [&](const ActiveRun& active) {
				VectorRun run;
				run.fillIndex = active.fillIndex;
				run.x1 = bounds.xmin + static_cast<int32_t>((static_cast<int64_t>(active.x1) * widthTwips) / workW);
				run.x2 = bounds.xmin + static_cast<int32_t>((static_cast<int64_t>(active.x2) * widthTwips) / workW);
				run.y1 = bounds.ymin + static_cast<int32_t>((static_cast<int64_t>(active.y1) * heightTwips) / workH);
				run.y2 = bounds.ymin + static_cast<int32_t>((static_cast<int64_t>(active.y2) * heightTwips) / workH);
				if (run.x2 > run.x1 && run.y2 > run.y1) {
					runs.push_back(run);
				}
				};

			std::vector<ActiveRun> activeRuns;
			activeRuns.reserve(workW);
			for (uint16_t y = 0; y < workH; ++y) {
				for (ActiveRun& active : activeRuns) {
					active.extended = false;
				}
				uint16_t x = 0;
				while (x < workW) {
					const uint32_t key = sampleKey(x, y);
					const uint8_t alpha = static_cast<uint8_t>((key >> 24) & 0xFF);
					if (alpha == 0) {
						++x;
						continue;
					}
					uint16_t x2 = static_cast<uint16_t>(x + 1);
					while (x2 < workW) {
						if (sampleKey(x2, y) != key) {
							break;
						}
						++x2;
					}
					const uint16_t fillIndex = fillForColor(key);
					if (fillIndex != 0) {
						ActiveRun* match = nullptr;
						for (ActiveRun& active : activeRuns) {
							if (!active.extended && active.key == key && active.fillIndex == fillIndex
								&& active.x1 == x && active.x2 == x2 && active.y2 == y) {
								match = &active;
								break;
							}
						}
						if (match) {
							match->y2 = static_cast<uint16_t>(y + 1);
							match->extended = true;
						}
						else {
							ActiveRun active;
							active.key = key;
							active.fillIndex = fillIndex;
							active.x1 = x;
							active.x2 = x2;
							active.y1 = y;
							active.y2 = static_cast<uint16_t>(y + 1);
							active.extended = true;
							activeRuns.push_back(active);
						}
					}
					x = x2;
				}
				for (size_t i = 0; i < activeRuns.size();) {
					if (activeRuns[i].extended) {
						++i;
						continue;
					}
					flushRun(activeRuns[i]);
					activeRuns.erase(activeRuns.begin() + static_cast<std::ptrdiff_t>(i));
				}
			}
			for (const ActiveRun& active : activeRuns) {
				flushRun(active);
			}

			std::vector<uint8_t> out;
			WriteU16(out, portrait.shapeId);
			out.insert(out.end(), boundsBytes.begin(), boundsBytes.end());
			if (fills.size() >= 255) {
				out.push_back(0xFF);
				WriteU16(out, static_cast<uint16_t>(fills.size()));
			}
			else {
				out.push_back(static_cast<uint8_t>(fills.size()));
			}
			for (const ColorRgba& fill : fills) {
				out.push_back(0x00);
				out.push_back(fill.r);
				out.push_back(fill.g);
				out.push_back(fill.b);
				out.push_back(fill.a);
			}
			out.push_back(0);

			std::vector<uint8_t> bits;
			const int fillBits = BitsNeeded(static_cast<uint32_t>(fills.size()));
			PutBits(bits, static_cast<uint32_t>(fillBits), 4);
			PutBits(bits, 0, 4);
			for (const VectorRun& run : runs) {
				PutStyleChangeMoveFill(bits, run.x1, run.y1, run.fillIndex, fillBits);
				PutStraightEdge(bits, run.x2 - run.x1, 0);
				PutStraightEdge(bits, 0, run.y2 - run.y1);
				PutStraightEdge(bits, run.x1 - run.x2, 0);
				PutStraightEdge(bits, 0, run.y1 - run.y2);
			}
			PutBits(bits, 0, 1);
			PutBits(bits, 0, 5);
			const std::vector<uint8_t> packed = PackBits(bits);
			out.insert(out.end(), packed.begin(), packed.end());
			Util::Logger::Instance().Get()->info(
				"[SWFInject] vectorized portrait source='{}' shape_id={} fills={} runs={} bounds=[{},{} -> {},{}] png={}x{} shape_bytes={}.",
				portrait.sourcePath,
				portrait.shapeId,
				fills.size(),
				runs.size(),
				bounds.xmin,
				bounds.ymin,
				bounds.xmax,
				bounds.ymax,
				portrait.image.width,
				portrait.image.height,
				out.size());
			return out;
		}

		std::vector<uint8_t> EncodeMetadata(const PortraitDefinition& portrait) {
			std::ostringstream s;
			s << "<outsideSWF kind=\"portrait\" shapeId=\"" << portrait.shapeId
				<< "\" source=\"" << portrait.sourcePath
				<< "\" character=\"" << portrait.characterId << "\" />";
			const std::string text = s.str();
			return std::vector<uint8_t>(text.begin(), text.end());
		}

		bool DecodeImageWic(const std::string& path, ImageRgba& outImage) {
			const HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			const bool coUninit = SUCCEEDED(coInit);
			IWICImagingFactory* factory = nullptr;
			HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
			if (FAILED(hr) || !factory) {
				if (coUninit) CoUninitialize();
				return false;
			}

			IWICBitmapDecoder* decoder = nullptr;
			const std::wstring widePath = WidenUtf8(path);
			hr = factory->CreateDecoderFromFilename(widePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
			if (FAILED(hr) || !decoder) {
				factory->Release();
				if (coUninit) CoUninitialize();
				return false;
			}
			IWICBitmapFrameDecode* frame = nullptr;
			hr = decoder->GetFrame(0, &frame);
			if (FAILED(hr) || !frame) {
				decoder->Release();
				factory->Release();
				if (coUninit) CoUninitialize();
				return false;
			}
			IWICFormatConverter* converter = nullptr;
			hr = factory->CreateFormatConverter(&converter);
			if (SUCCEEDED(hr) && converter) {
				hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
			}
			UINT width = 0;
			UINT height = 0;
			if (SUCCEEDED(hr)) {
				hr = converter->GetSize(&width, &height);
			}
			if (SUCCEEDED(hr) && width > 0 && height > 0 && width <= 65535 && height <= 65535) {
				std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
				hr = converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(rgba.size()), rgba.data());
				if (SUCCEEDED(hr)) {
					outImage.width = static_cast<uint16_t>(width);
					outImage.height = static_cast<uint16_t>(height);
					outImage.rgba = std::move(rgba);
				}
			}
			if (converter) converter->Release();
			frame->Release();
			decoder->Release();
			factory->Release();
			if (coUninit) CoUninitialize();
			return SUCCEEDED(hr) && !outImage.rgba.empty();
		}

		bool EndsWithInsensitive(const std::string& value, const char* suffix) {
			const size_t suffixLen = std::strlen(suffix);
			if (value.size() < suffixLen) {
				return false;
			}
			for (size_t i = 0; i < suffixLen; ++i) {
				const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(value[value.size() - suffixLen + i])));
				const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
				if (a != b) {
					return false;
				}
			}
			return true;
		}

		std::string ReadTextFile(const std::string& path) {
			std::ifstream file(path, std::ios::binary);
			if (!file) {
				return {};
			}
			std::ostringstream stream;
			stream << file.rdbuf();
			return stream.str();
		}

		std::string SvgAttribute(const std::string& tag, const char* name) {
			size_t pos = tag.find(name);
			while (pos != std::string::npos) {
				const bool leftOk = pos == 0 || std::isspace(static_cast<unsigned char>(tag[pos - 1])) || tag[pos - 1] == '<';
				size_t cur = pos + std::strlen(name);
				while (cur < tag.size() && std::isspace(static_cast<unsigned char>(tag[cur]))) {
					++cur;
				}
				if (leftOk && cur < tag.size() && tag[cur] == '=') {
					++cur;
					while (cur < tag.size() && std::isspace(static_cast<unsigned char>(tag[cur]))) {
						++cur;
					}
					if (cur < tag.size() && (tag[cur] == '"' || tag[cur] == '\'')) {
						const char quote = tag[cur++];
						const size_t end = tag.find(quote, cur);
						if (end != std::string::npos) {
							return tag.substr(cur, end - cur);
						}
					}
				}
				pos = tag.find(name, pos + 1);
			}
			return {};
		}

		float ParseSvgFloat(const std::string& value, float fallback = 0.0f) {
			if (value.empty()) {
				return fallback;
			}
			char* end = nullptr;
			const float parsed = std::strtof(value.c_str(), &end);
			return end != value.c_str() ? parsed : fallback;
		}

		ColorRgba ParseSvgFill(const std::string& tag) {
			std::string fill = SvgAttribute(tag, "fill");
			const std::string style = SvgAttribute(tag, "style");
			if (fill.empty() && !style.empty()) {
				const size_t pos = style.find("fill:");
				if (pos != std::string::npos) {
					size_t start = pos + 5;
					while (start < style.size() && std::isspace(static_cast<unsigned char>(style[start]))) {
						++start;
					}
					size_t end = style.find(';', start);
					if (end == std::string::npos) {
						end = style.size();
					}
					fill = style.substr(start, end - start);
				}
			}
			if (fill == "none") {
				return { 0, 0, 0, 0 };
			}
			if (!fill.empty() && fill[0] == '#') {
				if (fill.size() == 4) {
					const auto hex = [](char c) -> uint8_t {
						if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
						if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + c - 'a');
						if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + c - 'A');
						return 0;
						};
					const uint8_t r = hex(fill[1]);
					const uint8_t g = hex(fill[2]);
					const uint8_t b = hex(fill[3]);
					return { static_cast<uint8_t>((r << 4) | r), static_cast<uint8_t>((g << 4) | g), static_cast<uint8_t>((b << 4) | b), 255 };
				}
				if (fill.size() >= 7) {
					const uint32_t rgb = static_cast<uint32_t>(std::strtoul(fill.c_str() + 1, nullptr, 16));
					return {
						static_cast<uint8_t>((rgb >> 16) & 0xFF),
						static_cast<uint8_t>((rgb >> 8) & 0xFF),
						static_cast<uint8_t>(rgb & 0xFF),
						255
					};
				}
			}
			return { 255, 255, 255, 255 };
		}

		std::vector<float> ParseSvgNumbers(const std::string& text) {
			std::vector<float> numbers;
			const char* p = text.c_str();
			while (*p) {
				if (std::isdigit(static_cast<unsigned char>(*p)) || *p == '-' || *p == '+' || *p == '.') {
					char* end = nullptr;
					const float value = std::strtof(p, &end);
					if (end != p) {
						numbers.push_back(value);
						p = end;
						continue;
					}
				}
				++p;
			}
			return numbers;
		}

		SvgTransform MultiplySvgTransform(const SvgTransform& left, const SvgTransform& right) {
			return {
				left.a * right.a + left.c * right.b,
				left.b * right.a + left.d * right.b,
				left.a * right.c + left.c * right.d,
				left.b * right.c + left.d * right.d,
				left.a * right.e + left.c * right.f + left.e,
				left.b * right.e + left.d * right.f + left.f
			};
		}

		SvgTransform ParseSvgTransform(const std::string& value) {
			SvgTransform out;
			size_t pos = 0;
			while (pos < value.size()) {
				while (pos < value.size() && std::isspace(static_cast<unsigned char>(value[pos]))) {
					++pos;
				}
				const size_t nameStart = pos;
				while (pos < value.size() && std::isalpha(static_cast<unsigned char>(value[pos]))) {
					++pos;
				}
				if (nameStart == pos) {
					break;
				}
				const std::string name = value.substr(nameStart, pos - nameStart);
				const size_t open = value.find('(', pos);
				if (open == std::string::npos) {
					break;
				}
				const size_t close = value.find(')', open + 1);
				if (close == std::string::npos) {
					break;
				}
				const std::vector<float> n = ParseSvgNumbers(value.substr(open + 1, close - open - 1));
				SvgTransform next;
				if (name == "translate" && !n.empty()) {
					next.e = n[0];
					next.f = n.size() > 1 ? n[1] : 0.0f;
				}
				else if (name == "scale" && !n.empty()) {
					next.a = n[0];
					next.d = n.size() > 1 ? n[1] : n[0];
				}
				else if (name == "matrix" && n.size() >= 6) {
					next.a = n[0];
					next.b = n[1];
					next.c = n[2];
					next.d = n[3];
					next.e = n[4];
					next.f = n[5];
				}
				else {
					Util::Logger::Instance().Get()->warn("[SWFInject] unsupported SVG transform '{}'; keeping partial transform.", name);
				}
				out = MultiplySvgTransform(out, next);
				pos = close + 1;
			}
			return out;
		}

		SvgPoint ApplySvgTransform(const SvgTransform& transform, const SvgPoint& point) {
			return {
				transform.a * point.x + transform.c * point.y + transform.e,
				transform.b * point.x + transform.d * point.y + transform.f
			};
		}

		std::vector<SvgPoint> ApplySvgTransform(const SvgTransform& transform, const std::vector<SvgPoint>& points) {
			std::vector<SvgPoint> out;
			out.reserve(points.size());
			for (const SvgPoint& point : points) {
				out.push_back(ApplySvgTransform(transform, point));
			}
			return out;
		}

		void AddSvgShape(SvgDocument& doc, const ColorRgba& fill, const std::vector<SvgPoint>& points) {
			if (fill.a == 0 || points.size() < 3) {
				return;
			}
			doc.shapes.push_back({ fill, points });
		}

		void ParseSvgPathData(SvgDocument& doc, const std::string& d, const ColorRgba& fill, const SvgTransform& transform) {
			size_t i = 0;
			char cmd = 0;
			SvgPoint cur{};
			SvgPoint start{};
			std::vector<SvgPoint> poly;
			auto skip = [&]() {
				while (i < d.size() && (std::isspace(static_cast<unsigned char>(d[i])) || d[i] == ',')) {
					++i;
				}
				};
			auto hasNumber = [&]() {
				skip();
				return i < d.size() && (std::isdigit(static_cast<unsigned char>(d[i])) || d[i] == '-' || d[i] == '+' || d[i] == '.');
				};
			auto read = [&]() {
				skip();
				char* end = nullptr;
				const float v = std::strtof(d.c_str() + i, &end);
				i = static_cast<size_t>(end - d.c_str());
				return v;
				};
			auto closePoly = [&]() {
				if (poly.size() >= 3) {
					AddSvgShape(doc, fill, ApplySvgTransform(transform, poly));
				}
				poly.clear();
				};
			while (i < d.size()) {
				skip();
				if (i >= d.size()) break;
				if (std::isalpha(static_cast<unsigned char>(d[i]))) {
					cmd = d[i++];
				}
				const bool rel = std::islower(static_cast<unsigned char>(cmd)) != 0;
				const char op = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));
				if (op == 'M') {
					if (!hasNumber()) continue;
					closePoly();
					float x = read();
					float y = read();
					if (rel) { x += cur.x; y += cur.y; }
					cur = { x, y };
					start = cur;
					poly.push_back(cur);
					cmd = rel ? 'l' : 'L';
				}
				else if (op == 'L') {
					while (hasNumber()) {
						float x = read();
						float y = read();
						if (rel) { x += cur.x; y += cur.y; }
						cur = { x, y };
						poly.push_back(cur);
					}
				}
				else if (op == 'H') {
					while (hasNumber()) {
						float x = read();
						if (rel) x += cur.x;
						cur.x = x;
						poly.push_back(cur);
					}
				}
				else if (op == 'V') {
					while (hasNumber()) {
						float y = read();
						if (rel) y += cur.y;
						cur.y = y;
						poly.push_back(cur);
					}
				}
				else if (op == 'C') {
					while (hasNumber()) {
						SvgPoint c1{ read(), read() };
						SvgPoint c2{ read(), read() };
						SvgPoint end{ read(), read() };
						if (rel) {
							c1.x += cur.x; c1.y += cur.y;
							c2.x += cur.x; c2.y += cur.y;
							end.x += cur.x; end.y += cur.y;
						}
						const SvgPoint p0 = cur;
						for (int step = 1; step <= 12; ++step) {
							const float t = static_cast<float>(step) / 12.0f;
							const float u = 1.0f - t;
							poly.push_back({
								u * u * u * p0.x + 3.0f * u * u * t * c1.x + 3.0f * u * t * t * c2.x + t * t * t * end.x,
								u * u * u * p0.y + 3.0f * u * u * t * c1.y + 3.0f * u * t * t * c2.y + t * t * t * end.y
								});
						}
						cur = end;
					}
				}
				else if (op == 'Q') {
					while (hasNumber()) {
						SvgPoint c{ read(), read() };
						SvgPoint end{ read(), read() };
						if (rel) {
							c.x += cur.x; c.y += cur.y;
							end.x += cur.x; end.y += cur.y;
						}
						const SvgPoint p0 = cur;
						for (int step = 1; step <= 12; ++step) {
							const float t = static_cast<float>(step) / 12.0f;
							const float u = 1.0f - t;
							poly.push_back({
								u * u * p0.x + 2.0f * u * t * c.x + t * t * end.x,
								u * u * p0.y + 2.0f * u * t * c.y + t * t * end.y
								});
						}
						cur = end;
					}
				}
				else if (op == 'Z') {
					poly.push_back(start);
					closePoly();
				}
				else {
					Util::Logger::Instance().Get()->warn("[SWFInject] unsupported SVG path command '{}'; partial path converted.", cmd);
					break;
				}
			}
			closePoly();
		}

		bool LoadSvgDocument(const std::string& path, SvgDocument& doc) {
			const std::string text = ReadTextFile(path);
			if (text.empty()) {
				return false;
			}
			const size_t svgStart = text.find("<svg");
			const size_t svgEnd = svgStart == std::string::npos ? std::string::npos : text.find('>', svgStart);
			if (svgEnd == std::string::npos) {
				return false;
			}
			const std::string svgTag = text.substr(svgStart, svgEnd - svgStart + 1);
			doc.width = ParseSvgFloat(SvgAttribute(svgTag, "width"), 0.0f);
			doc.height = ParseSvgFloat(SvgAttribute(svgTag, "height"), 0.0f);
			const std::string viewBox = SvgAttribute(svgTag, "viewBox");
			const std::vector<float> vb = ParseSvgNumbers(viewBox);
			if (vb.size() >= 4 && vb[2] > 0.0f && vb[3] > 0.0f) {
				doc.viewMinX = vb[0];
				doc.viewMinY = vb[1];
				doc.viewWidth = vb[2];
				doc.viewHeight = vb[3];
			}
			if ((doc.width <= 0.0f || doc.height <= 0.0f) && vb.size() >= 4) {
				doc.width = vb[2];
				doc.height = vb[3];
			}
			if (doc.width <= 0.0f || doc.height <= 0.0f) {
				return false;
			}
			if (doc.viewWidth <= 0.0f || doc.viewHeight <= 0.0f) {
				doc.viewWidth = doc.width;
				doc.viewHeight = doc.height;
			}
			if (text.find("<g") != std::string::npos && text.find("transform=") != std::string::npos) {
				Util::Logger::Instance().Get()->warn(
					"[SWFInject] SVG portrait '{}' contains group transforms; direct element transforms are applied, inherited group transforms are not supported yet.",
					path);
			}

			auto forEachTag = [&](const char* name, const auto& fn) {
				std::string open = std::string("<") + name;
				size_t pos = 0;
				while ((pos = text.find(open, pos)) != std::string::npos) {
					const size_t end = text.find('>', pos);
					if (end == std::string::npos) break;
					fn(text.substr(pos, end - pos + 1));
					pos = end + 1;
				}
				};

			forEachTag("path", [&](const std::string& tag) {
				ParseSvgPathData(doc, SvgAttribute(tag, "d"), ParseSvgFill(tag), ParseSvgTransform(SvgAttribute(tag, "transform")));
				});
			forEachTag("polygon", [&](const std::string& tag) {
				const ColorRgba fill = ParseSvgFill(tag);
				const SvgTransform transform = ParseSvgTransform(SvgAttribute(tag, "transform"));
				const std::vector<float> n = ParseSvgNumbers(SvgAttribute(tag, "points"));
				std::vector<SvgPoint> points;
				for (size_t i = 0; i + 1 < n.size(); i += 2) {
					points.push_back({ n[i], n[i + 1] });
				}
				AddSvgShape(doc, fill, ApplySvgTransform(transform, points));
				});
			forEachTag("polyline", [&](const std::string& tag) {
				const ColorRgba fill = ParseSvgFill(tag);
				const SvgTransform transform = ParseSvgTransform(SvgAttribute(tag, "transform"));
				const std::vector<float> n = ParseSvgNumbers(SvgAttribute(tag, "points"));
				std::vector<SvgPoint> points;
				for (size_t i = 0; i + 1 < n.size(); i += 2) {
					points.push_back({ n[i], n[i + 1] });
				}
				AddSvgShape(doc, fill, ApplySvgTransform(transform, points));
				});
			forEachTag("rect", [&](const std::string& tag) {
				const float x = ParseSvgFloat(SvgAttribute(tag, "x"));
				const float y = ParseSvgFloat(SvgAttribute(tag, "y"));
				const float w = ParseSvgFloat(SvgAttribute(tag, "width"));
				const float h = ParseSvgFloat(SvgAttribute(tag, "height"));
				const SvgTransform transform = ParseSvgTransform(SvgAttribute(tag, "transform"));
				AddSvgShape(doc, ParseSvgFill(tag), ApplySvgTransform(transform, { {x, y}, {x + w, y}, {x + w, y + h}, {x, y + h} }));
				});
			auto ellipseFn = [&](const std::string& tag, bool circle) {
				const float cx = ParseSvgFloat(SvgAttribute(tag, "cx"));
				const float cy = ParseSvgFloat(SvgAttribute(tag, "cy"));
				const float rx = circle ? ParseSvgFloat(SvgAttribute(tag, "r")) : ParseSvgFloat(SvgAttribute(tag, "rx"));
				const float ry = circle ? rx : ParseSvgFloat(SvgAttribute(tag, "ry"));
				const SvgTransform transform = ParseSvgTransform(SvgAttribute(tag, "transform"));
				std::vector<SvgPoint> points;
				for (int i = 0; i < 32; ++i) {
					const float a = static_cast<float>(i) * 6.28318530718f / 32.0f;
					points.push_back({ cx + std::cos(a) * rx, cy + std::sin(a) * ry });
				}
				AddSvgShape(doc, ParseSvgFill(tag), ApplySvgTransform(transform, points));
				};
			forEachTag("circle", [&](const std::string& tag) { ellipseFn(tag, true); });
			forEachTag("ellipse", [&](const std::string& tag) { ellipseFn(tag, false); });

			Util::Logger::Instance().Get()->info(
				"[SWFInject] loaded SVG portrait '{}' size={}x{} viewBox=[{},{} {}x{}] shapes={}.",
				path,
				doc.width,
				doc.height,
				doc.viewMinX,
				doc.viewMinY,
				doc.viewWidth,
				doc.viewHeight,
				doc.shapes.size());
			return !doc.shapes.empty();
		}

		bool IsCharacterIdTag(uint16_t code) {
			switch (code) {
			case 2: case 6: case 7: case 10: case 11: case 14: case 20: case 21: case 22: case 32:
			case 33: case 34: case 35: case 36: case 37: case 39: case 46: case 48: case 60: case 75:
			case 83: case 84: case 87: case 90: case 91:
				return true;
			default:
				return false;
			}
		}

		uint16_t MaxCharacterId(const uint8_t* tags, const uint8_t* end) {
			uint16_t maxId = 0;
			const uint8_t* cursor = tags;
			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					break;
				}
				if (tag.code == 0) {
					break;
				}
				if (IsCharacterIdTag(tag.code) && tag.length >= 2) {
					maxId = std::max(maxId, ReadU16(tag.payload));
				}
				cursor = tag.next;
			}
			return maxId;
		}

		size_t RectByteLength(const uint8_t* p, size_t length) {
			if (!p || length == 0) {
				return 0;
			}
			const uint8_t nbits = static_cast<uint8_t>(p[0] >> 3);
			const size_t bitLength = 5u + 4u * nbits;
			const size_t byteLength = (bitLength + 7u) / 8u;
			return byteLength <= length ? byteLength : 0;
		}

		std::vector<uint8_t> FindShapePayload(const SWFBufferView& view, uint16_t shapeId) {
			const uint8_t* cursor = view.tags;
			const uint8_t* end = view.base + view.fileLength;
			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					break;
				}
				if (tag.code == 0) {
					break;
				}
				if ((tag.code == 2 || tag.code == 22 || tag.code == 32) && tag.length >= 2 && ReadU16(tag.payload) == shapeId) {
					return std::vector<uint8_t>(tag.payload, tag.next);
				}
				cursor = tag.next;
			}
			return {};
		}

		bool ReplaceFirstBitmapFillId(std::vector<uint8_t>& shapePayload, uint16_t bitmapId) {
			if (shapePayload.size() < 4) {
				return false;
			}
			const size_t rectLen = RectByteLength(shapePayload.data() + 2, shapePayload.size() - 2);
			if (rectLen == 0) {
				return false;
			}
			size_t pos = 2 + rectLen;
			if (pos >= shapePayload.size()) {
				return false;
			}
			uint16_t fillCount = shapePayload[pos++];
			if (fillCount == 0xFF) {
				if (pos + 2 > shapePayload.size()) {
					return false;
				}
				fillCount = ReadU16(shapePayload.data() + pos);
				pos += 2;
			}
			for (uint16_t i = 0; i < fillCount && pos < shapePayload.size(); ++i) {
				const uint8_t fillType = shapePayload[pos++];
				if ((fillType == 0x40 || fillType == 0x41 || fillType == 0x42 || fillType == 0x43) && pos + 2 <= shapePayload.size()) {
					PatchU16(shapePayload, pos, bitmapId);
					return true;
				}
				return false;
			}
			return false;
		}

		std::vector<uint8_t> CloneShapeTemplateWithBitmap(const std::vector<uint8_t>& shapeTemplate, const PortraitDefinition& portrait) {
			std::vector<uint8_t> out = shapeTemplate;
			PatchU16(out, 0, portrait.shapeId);
			if (!ReplaceFirstBitmapFillId(out, portrait.bitmapId)) {
				Util::Logger::Instance().Get()->warn(
					"[SWFInject] failed to patch bitmap id inside shape 152 template; falling back to generated shape for shape_id={}.",
					portrait.shapeId);
				return {};
			}
			return out;
		}

		std::vector<uint8_t> EncodeBitmapFillShape(
			const PortraitDefinition& portrait,
			const RectI32& bounds,
			const std::vector<uint8_t>& boundsBytes) {
			const int32_t widthTwips = std::max<int32_t>(1, bounds.xmax - bounds.xmin);
			const int32_t heightTwips = std::max<int32_t>(1, bounds.ymax - bounds.ymin);
			const float scaleX = static_cast<float>(widthTwips) / (static_cast<float>(portrait.image.width) * 20.0f);
			const float scaleY = static_cast<float>(heightTwips) / (static_cast<float>(portrait.image.height) * 20.0f);

			std::vector<uint8_t> out;
			WriteU16(out, portrait.shapeId);
			out.insert(out.end(), boundsBytes.begin(), boundsBytes.end());

			out.push_back(1);
			out.push_back(0x41);
			WriteU16(out, portrait.bitmapId);
			const std::vector<uint8_t> matrix = EncodeMatrix(scaleX, scaleY);
			out.insert(out.end(), matrix.begin(), matrix.end());
			out.push_back(0);

			std::vector<uint8_t> bits;
			PutBits(bits, 1, 4);
			PutBits(bits, 0, 4);
			PutStyleChangeMoveFill(bits, bounds.xmin, bounds.ymin, 1, 1);
			PutStraightEdge(bits, widthTwips, 0);
			PutStraightEdge(bits, 0, heightTwips);
			PutStraightEdge(bits, -widthTwips, 0);
			PutStraightEdge(bits, 0, -heightTwips);
			PutBits(bits, 0, 1);
			PutBits(bits, 0, 5);
			const std::vector<uint8_t> packed = PackBits(bits);
			out.insert(out.end(), packed.begin(), packed.end());
			Util::Logger::Instance().Get()->info(
				"[SWFInject] generated bitmap-fill shape source='{}' bitmap_id={} shape_id={} bounds=[{},{} -> {},{}] scale=({}, {}) shape_tag_bytes={}.",
				portrait.sourcePath,
				portrait.bitmapId,
				portrait.shapeId,
				bounds.xmin,
				bounds.ymin,
				bounds.xmax,
				bounds.ymax,
				scaleX,
				scaleY,
				out.size());
			return out;
		}

		std::vector<uint8_t> EncodeSvgShape(
			const PortraitDefinition& portrait,
			const RectI32& bounds,
			const std::vector<uint8_t>& boundsBytes) {
			const int32_t widthTwips = std::max<int32_t>(1, bounds.xmax - bounds.xmin);
			const int32_t heightTwips = std::max<int32_t>(1, bounds.ymax - bounds.ymin);
			static constexpr size_t kMaxSvgPortraitFills = 63;
			std::vector<ColorRgba> fills;
			std::vector<uint16_t> shapeFillIndices;
			shapeFillIndices.reserve(portrait.svg.shapes.size());
			size_t mergedFills = 0;
			auto colorDistance = [](const ColorRgba& a, const ColorRgba& b) -> uint32_t {
				const int dr = static_cast<int>(a.r) - static_cast<int>(b.r);
				const int dg = static_cast<int>(a.g) - static_cast<int>(b.g);
				const int db = static_cast<int>(a.b) - static_cast<int>(b.b);
				const int da = static_cast<int>(a.a) - static_cast<int>(b.a);
				return static_cast<uint32_t>(dr * dr + dg * dg + db * db + da * da);
				};
			for (const SvgShape& shape : portrait.svg.shapes) {
				uint16_t fillIndex = 0;
				for (size_t i = 0; i < fills.size(); ++i) {
					const ColorRgba& existing = fills[i];
					if (existing.r == shape.fill.r && existing.g == shape.fill.g && existing.b == shape.fill.b && existing.a == shape.fill.a) {
						fillIndex = static_cast<uint16_t>(i + 1);
						break;
					}
				}
				if (fillIndex == 0) {
					if (fills.size() < kMaxSvgPortraitFills) {
						fills.push_back(shape.fill);
						fillIndex = static_cast<uint16_t>(fills.size());
					}
					else {
						size_t best = 0;
						uint32_t bestDistance = UINT32_MAX;
						for (size_t i = 0; i < fills.size(); ++i) {
							const uint32_t distance = colorDistance(shape.fill, fills[i]);
							if (distance < bestDistance) {
								bestDistance = distance;
								best = i;
							}
						}
						fillIndex = static_cast<uint16_t>(best + 1);
						++mergedFills;
					}
				}
				shapeFillIndices.push_back(fillIndex);
			}

			const float scale = std::min(
				static_cast<float>(widthTwips) / portrait.svg.viewWidth,
				static_cast<float>(heightTwips) / portrait.svg.viewHeight);
			const float drawnWidth = portrait.svg.viewWidth * scale;
			const float drawnHeight = portrait.svg.viewHeight * scale;
			const float offsetX = static_cast<float>(bounds.xmin) + (static_cast<float>(widthTwips) - drawnWidth) * 0.5f;
			const float offsetY = static_cast<float>(bounds.ymin) + (static_cast<float>(heightTwips) - drawnHeight) * 0.5f;
			auto mapPoint = [&](const SvgPoint& point) -> std::pair<int32_t, int32_t> {
				const int32_t x = static_cast<int32_t>(std::lround(offsetX + (point.x - portrait.svg.viewMinX) * scale));
				const int32_t y = static_cast<int32_t>(std::lround(offsetY + (point.y - portrait.svg.viewMinY) * scale));
				return { x, y };
				};

			std::vector<uint8_t> out;
			WriteU16(out, portrait.shapeId);
			out.insert(out.end(), boundsBytes.begin(), boundsBytes.end());
			if (fills.size() >= 255) {
				out.push_back(0xFF);
				WriteU16(out, static_cast<uint16_t>(fills.size()));
			}
			else {
				out.push_back(static_cast<uint8_t>(fills.size()));
			}
			for (const ColorRgba& fill : fills) {
				out.push_back(0x00);
				out.push_back(fill.r);
				out.push_back(fill.g);
				out.push_back(fill.b);
				out.push_back(fill.a);
			}
			out.push_back(0);

			std::vector<uint8_t> bits;
			const int fillBits = 6;
			PutBits(bits, static_cast<uint32_t>(fillBits), 4);
			PutBits(bits, 0, 4);
			for (size_t i = 0; i < portrait.svg.shapes.size(); ++i) {
				const SvgShape& shape = portrait.svg.shapes[i];
				if (shape.points.size() < 3) {
					continue;
				}
				const auto first = mapPoint(shape.points.front());
				PutStyleChangeMoveFill(bits, first.first, first.second, shapeFillIndices[i], fillBits);
				int32_t prevX = first.first;
				int32_t prevY = first.second;
				for (size_t p = 1; p < shape.points.size(); ++p) {
					const auto mapped = mapPoint(shape.points[p]);
					if (mapped.first == prevX && mapped.second == prevY) {
						continue;
					}
					PutStraightEdge(bits, mapped.first - prevX, mapped.second - prevY);
					prevX = mapped.first;
					prevY = mapped.second;
				}
				if (first.first != prevX || first.second != prevY) {
					PutStraightEdge(bits, first.first - prevX, first.second - prevY);
				}
			}
			PutBits(bits, 0, 1);
			PutBits(bits, 0, 5);
			const std::vector<uint8_t> packed = PackBits(bits);
			out.insert(out.end(), packed.begin(), packed.end());
			Util::Logger::Instance().Get()->info(
				"[SWFInject] encoded SVG shape source='{}' shape_id={} fills={} merged_fills={} fill_bits={} paths={} bounds=[{},{} -> {},{}] viewBox=[{},{} {}x{}] scale={} offset=({}, {}) shape_tag_bytes={}.",
				portrait.sourcePath,
				portrait.shapeId,
				fills.size(),
				mergedFills,
				fillBits,
				portrait.svg.shapes.size(),
				bounds.xmin,
				bounds.ymin,
				bounds.xmax,
				bounds.ymax,
				portrait.svg.viewMinX,
				portrait.svg.viewMinY,
				portrait.svg.viewWidth,
				portrait.svg.viewHeight,
				scale,
				offsetX,
				offsetY,
				out.size());
			return out;
		}

		bool PlaceObject2DepthAndCharacterOffset(const std::vector<uint8_t>& payload, uint16_t* depthOut, size_t* characterOffsetOut) {
			if (payload.size() < 3) {
				return false;
			}
			const uint8_t flags = payload[0];
			if ((flags & 0x02) == 0) {
				return false;
			}
			const uint16_t depth = ReadU16(payload.data() + 1);
			if (depthOut) {
				*depthOut = depth;
			}
			if (characterOffsetOut) {
				*characterOffsetOut = 3;
			}
			return payload.size() >= 5;
		}

		std::vector<uint8_t> ClonePlaceObject2WithCharacter(const std::vector<uint8_t>& templatePayload, uint16_t characterId) {
			std::vector<uint8_t> out = templatePayload;
			uint16_t depth = 0;
			size_t characterOffset = 0;
			if (!PlaceObject2DepthAndCharacterOffset(out, &depth, &characterOffset)) {
				return {};
			}
			out[0] |= 0x03;
			PatchU16(out, 1, 1);
			PatchU16(out, characterOffset, characterId);
			return out;
		}

		bool PatchFirstDepth1PlaceObject2(std::vector<uint8_t>& framePayload, uint16_t shapeId, uint16_t* oldCharacterIdOut) {
			const uint8_t* base = framePayload.data();
			const uint8_t* cursor = base;
			const uint8_t* end = base + framePayload.size();
			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					return false;
				}
				if (tag.code == 26 && tag.length >= 5) {
					std::vector<uint8_t> place(tag.payload, tag.next);
					uint16_t depth = 0;
					size_t characterOffset = 0;
					if (PlaceObject2DepthAndCharacterOffset(place, &depth, &characterOffset) && depth == 1) {
						const size_t absoluteCharacterOffset = static_cast<size_t>(tag.payload - base) + characterOffset;
						if (absoluteCharacterOffset + 2 <= framePayload.size()) {
							if (oldCharacterIdOut) {
								*oldCharacterIdOut = ReadU16(framePayload.data() + absoluteCharacterOffset);
							}
							framePayload[static_cast<size_t>(tag.payload - base)] |= 0x03;
							PatchU16(framePayload, static_cast<size_t>(tag.payload - base) + 1, 1);
							PatchU16(framePayload, absoluteCharacterOffset, shapeId);
							return true;
						}
					}
				}
				cursor = tag.next;
			}
			return false;
		}

		void AppendClonedPortraitFrames(std::vector<uint8_t>& out, const std::vector<PortraitPlacement>& placements, uint16_t spriteId, int sourceFrame, const std::vector<uint8_t>& framePayload) {
			for (const PortraitPlacement& placement : placements) {
				std::vector<uint8_t> clonedFrame = framePayload;
				uint16_t oldCharacterId = 0;
				const bool patchedPlace = PatchFirstDepth1PlaceObject2(clonedFrame, placement.shapeId, &oldCharacterId);
				if (!patchedPlace) {
					Util::Logger::Instance().Get()->info(
						"[SWFInject] sprite={} clone_frame={} failed to find PlaceObject2 depth=1; appending fallback PlaceObject2 shape_id={}.",
						spriteId,
						sourceFrame,
						placement.shapeId);
					const std::vector<uint8_t> place = EncodePlaceObject2(placement.shapeId, 1);
					AppendTag(out, 26, place);
				}
				out.insert(out.end(), clonedFrame.begin(), clonedFrame.end());
				Util::Logger::Instance().Get()->info(
					"[SWFInject] sprite={} cloned frame {} -> new frame with PlaceObject2 depth=1 old_character_id={} new_shape_id={} cloned_frame_bytes={} patched_place={}.",
					spriteId,
					sourceFrame,
					oldCharacterId,
					placement.shapeId,
					clonedFrame.size(),
					patchedPlace);
			}
		}

		constexpr uint16_t kLobbyPortraitSpriteId = 179;

		bool PatchLobbyPortraitSprite(const uint8_t* payload, uint32_t length, const InjectionSet& injections, std::vector<uint8_t>& out) {
			if (!payload || length < 4 || ReadU16(payload) != kLobbyPortraitSpriteId) {
				return false;
			}
			const uint8_t* end = payload + length;
			out.clear();
			out.reserve(length + injections.freshPlacements.size() * 512);
			out.insert(out.end(), payload, payload + 4);
			const uint16_t oldFrameCount = ReadU16(payload + 2);
			const uint16_t addedFrames = static_cast<uint16_t>(std::min(
				static_cast<size_t>(65535),
				injections.freshPlacements.size()));
			const uint16_t newFrameCount = static_cast<uint16_t>(std::min(65535, static_cast<int>(oldFrameCount + addedFrames)));
			out[2] = static_cast<uint8_t>(newFrameCount & 0xFF);
			out[3] = static_cast<uint8_t>((newFrameCount >> 8) & 0xFF);
			Util::Logger::Instance().Get()->info(
				"[SWFInject] sprite={} frame_count old={} new={} clone_source_frame=135 fresh_to_insert={}.",
				kLobbyPortraitSpriteId,
				oldFrameCount,
				newFrameCount,
				injections.freshPlacements.size());

			const uint8_t* cursor = payload + 4;
			int frame = 0;
			bool insertedFresh = false;
			std::vector<uint8_t> currentFramePayload;
			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					break;
				}
				out.insert(out.end(), tag.begin, tag.next);
				currentFramePayload.insert(currentFramePayload.end(), tag.begin, tag.next);
				cursor = tag.next;
				if (tag.code == 1) {
					++frame;
					if (frame == 135 && !insertedFresh && !injections.freshPlacements.empty()) {
						AppendClonedPortraitFrames(out, injections.freshPlacements, kLobbyPortraitSpriteId, frame, currentFramePayload);
						insertedFresh = true;
					}
					currentFramePayload.clear();
				}
				if (tag.code == 0) {
					break;
				}
			}
			return insertedFresh;
		}

		void AppendDefinitions(std::vector<uint8_t>& out, const InjectionSet& injections) {
			Util::Logger::Instance().Get()->info(
				"[SWFInject] appending {} SVG portrait shape definitions before root End.",
				injections.definitions.size());
			for (const PortraitDefinition& portrait : injections.definitions) {
				const std::vector<uint8_t> metadata = EncodeMetadata(portrait);
				const std::vector<uint8_t> bounds = injections.shapeTemplate152BoundsBytes.empty()
					? EncodeRect(0, static_cast<int32_t>(portrait.svg.width) * 20, 0, static_cast<int32_t>(portrait.svg.height) * 20)
					: injections.shapeTemplate152BoundsBytes;
				const std::vector<uint8_t> shape = EncodeSvgShape(
					portrait,
					injections.shapeTemplate152Bounds,
					bounds);
				Util::Logger::Instance().Get()->info(
					"[SWFInject] define SVG portrait character='{}' source='{}' shape_id={} tag=DefineShape3 force_shape_long_tag=true shape_tag_bytes={} bounds_template_152=true.",
					portrait.characterId,
					portrait.sourcePath,
					portrait.shapeId,
					shape.size());
				AppendTag(out, 77, metadata);
				AppendTagLong(out, 32, shape);
			}
		}

		InjectionSet BuildInjectionSet(uint8_t* swfBuffer, const SWFBufferView& view) {
			InjectionSet set;
			set.shapeTemplate152 = FindShapePayload(view, 152);
			if (set.shapeTemplate152.size() > 2) {
				size_t rectLen = 0;
				if (DecodeRect(set.shapeTemplate152.data() + 2, set.shapeTemplate152.size() - 2, set.shapeTemplate152Bounds, rectLen)) {
					set.shapeTemplate152BoundsBytes.assign(set.shapeTemplate152.begin() + 2, set.shapeTemplate152.begin() + 2 + rectLen);
				}
			}
			Util::Logger::Instance().Get()->info(
				"[SWFInject] shape template 152 found={} bytes={} bounds=[{},{} -> {},{}] bounds_bytes={}.",
				!set.shapeTemplate152.empty(),
				set.shapeTemplate152.size(),
				set.shapeTemplate152Bounds.xmin,
				set.shapeTemplate152Bounds.ymin,
				set.shapeTemplate152Bounds.xmax,
				set.shapeTemplate152Bounds.ymax,
				set.shapeTemplate152BoundsBytes.size());
			if (set.shapeTemplate152.empty()) {
				Util::Logger::Instance().Get()->warn("[SWFInject] skip lobby portrait injection: shape template 152 not found.");
				return set;
			}
			uint16_t nextId = static_cast<uint16_t>(MaxCharacterId(view.tags, view.base + view.fileLength) + 1);
			const auto& addons = Save::CharacterConfig::Instance().GetAddons();
			for (const auto& addon : addons) {
				if (addon.portraitClassicPath.empty() && addon.portraitFreshPath.empty()) {
					continue;
				}

				auto define = [&](const std::string& path) -> PortraitPlacement {
					for (const PortraitDefinition& existing : set.definitions) {
						if (existing.sourcePath == path) {
							return { existing.shapeId };
						}
					}
					if (!FileExistsA(path) || nextId > 65533) {
						return {};
					}
					if (!EndsWithInsensitive(path, ".svg")) {
						Util::Logger::Instance().Get()->warn("[SWFInject] portrait '{}' for character '{}' is not SVG; PNG/vector conversion is intentionally deferred.", path, addon.id);
						return {};
					}
					SvgDocument svg;
					if (!LoadSvgDocument(path, svg)) {
						Util::Logger::Instance().Get()->warn("[SWFInject] failed to parse SVG portrait '{}' for character '{}'.", path, addon.id);
						return {};
					}
					PortraitDefinition def;
					def.characterId = addon.id;
					def.sourcePath = path;
					def.shapeId = nextId++;
					def.svg = std::move(svg);
					def.isSvg = true;
					const PortraitPlacement placement{ def.shapeId };
					set.definitions.push_back(std::move(def));
					return placement;
					};

				const std::string freshPath = !addon.portraitFreshPath.empty() ? addon.portraitFreshPath : addon.portraitClassicPath;
				const PortraitPlacement freshPlacement = define(freshPath);
				if (freshPlacement.shapeId) {
					set.freshPlacements.push_back(freshPlacement);
				}
			}
			return set;
		}
	}

	bool TryInjectLobbyPortraits(SWFScene* scene, const std::string& swfPath) {
		if (!scene || !scene->swfBuffer) {
			return false;
		}
		if (!PathContainsLobby(swfPath)) {
			return false;
		}
		Util::Logger::Instance().Get()->info("[SWFInject] lobby candidate path='{}' scene={} swf={}.",
			swfPath,
			static_cast<void*>(scene),
			static_cast<void*>(scene->swfBuffer));

		SWFBufferView view = MakeSWFBufferView(scene->swfBuffer);
		if (!view.valid || view.compressed || !view.tags || view.fileLength < view.headerSize) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip lobby patch: invalid SWF view valid={} compressed={} tags={} file_length={} header_size={} sig='{}{}{}'.",
				view.valid,
				view.compressed,
				static_cast<const void*>(view.tags),
				view.fileLength,
				view.headerSize,
				scene->swfBuffer ? static_cast<char>(scene->swfBuffer[0]) : '?',
				scene->swfBuffer ? static_cast<char>(scene->swfBuffer[1]) : '?',
				scene->swfBuffer ? static_cast<char>(scene->swfBuffer[2]) : '?');
			return false;
		}

		InjectionSet injections = BuildInjectionSet(scene->swfBuffer, view);
		if (injections.classicPlacements.empty() && injections.freshPlacements.empty()) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip lobby patch: no portrait frames prepared addons={} definitions={} classic_frames={} fresh_frames={}.",
				Save::CharacterConfig::Instance().GetAddons().size(),
				injections.definitions.size(),
				injections.classicPlacements.size(),
				injections.freshPlacements.size());
			return false;
		}
		Util::Logger::Instance().Get()->info(
			"[SWFInject] prepared lobby portraits definitions={} classic_frames={} fresh_frames={} file_length={} header_size={}.",
			injections.definitions.size(),
			injections.classicPlacements.size(),
			injections.freshPlacements.size(),
			view.fileLength,
			view.headerSize);

		std::vector<uint8_t> patched;
		patched.reserve(view.fileLength + injections.definitions.size() * 512 * 1024);
		patched.insert(patched.end(), view.base, view.tags);

		bool patchedSprite = false;
		const uint8_t* cursor = view.tags;
		const uint8_t* end = view.base + view.fileLength;
		while (cursor < end) {
			Tag tag;
			if (!ReadTag(cursor, end, tag)) {
				Util::Logger::Instance().Get()->warn("[SWFInject] skip lobby patch: failed to read root tag at offset {}.",
					static_cast<size_t>(cursor - view.base));
				return false;
			}
			if (tag.code == 0) {
				if (patchedSprite) {
					AppendDefinitions(patched, injections);
				}
				AppendTag(patched, 0, std::vector<uint8_t>{});
				break;
			}

			if (!patchedSprite && tag.code == 39 && tag.length >= 4 && ReadU16(tag.payload) == kLobbyPortraitSpriteId) {
				std::vector<uint8_t> spritePayload;
				if (PatchLobbyPortraitSprite(tag.payload, tag.length, injections, spritePayload)) {
					AppendTag(patched, 39, spritePayload);
					patchedSprite = true;
				}
				else {
					patched.insert(patched.end(), tag.begin, tag.next);
				}
			}
			else {
				patched.insert(patched.end(), tag.begin, tag.next);
			}
			cursor = tag.next;
		}

		if (!patchedSprite || patched.size() < sizeof(SWFHeader) || patched.size() > 64u * 1024u * 1024u) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip lobby patch: patched_sprite={} patched_size={} min_size={} max_size={}.",
				patchedSprite,
				patched.size(),
				sizeof(SWFHeader),
				64u * 1024u * 1024u);
			return false;
		}
		const uint32_t newLength = static_cast<uint32_t>(patched.size());
		patched[4] = static_cast<uint8_t>(newLength & 0xFF);
		patched[5] = static_cast<uint8_t>((newLength >> 8) & 0xFF);
		patched[6] = static_cast<uint8_t>((newLength >> 16) & 0xFF);
		patched[7] = static_cast<uint8_t>((newLength >> 24) & 0xFF);

		auto owned = std::make_unique<std::vector<uint8_t>>(std::move(patched));
		scene->swfBuffer = owned->data();
		g_ownedPatchedBuffers.push_back(std::move(owned));
		Util::Logger::Instance().Get()->info(
			"[SWFInject] patched lobby portraits definitions={} classic_frames={} fresh_frames={} old_size={} new_size={}.",
			injections.definitions.size(),
			injections.classicPlacements.size(),
			injections.freshPlacements.size(),
			view.fileLength,
			newLength);
		return true;
	}
}