#include "SWFRuntimeInjector.h"
#include "MatchmakingSwfData.h"
#include "MapNewSwfData.h"
#include "OnlineCompatibilitySwfData.h"

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
#include <array>

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

		struct SvgPathSegment {
			bool curved = false;
			SvgPoint control;
			SvgPoint anchor;
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
			std::vector<SvgPathSegment> segments;
			size_t pathGroup = 0;
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
			std::vector<uint8_t> importedShapePayload;
			uint16_t bitmapId = 0;
			uint16_t shapeId = 0;
			uint16_t importedShapeTagCode = 0;
			ImageRgba image;
			SvgDocument svg;
			bool isSvg = false;
			bool isImportedSwf = false;
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
			std::vector<uint8_t> shapeTemplate151;
			uint16_t shapeTemplate151TagCode = 0;
			std::vector<uint8_t> shapeTemplate152;
			uint16_t shapeTemplate152TagCode = 0;
			RectI32 shapeTemplate152Bounds;
			std::vector<uint8_t> shapeTemplate152BoundsBytes;
		};

		struct RawTagDefinition {
			uint16_t code = 0;
			std::vector<uint8_t> payload;
		};

		struct MenuOnlineCompatibilityInjection {
			std::vector<RawTagDefinition> definitions;
			uint16_t exportedSpriteId = 0;
			uint16_t nextCharacterId = 0;
			bool valid = false;
		};


		struct MenuSwfImportInjection {
			std::vector<RawTagDefinition> definitions;
			uint16_t exportedSpriteId = 0;
			uint16_t nextCharacterId = 0;
			bool valid = false;
		};		std::vector<std::unique_ptr<std::vector<uint8_t>>> g_ownedPatchedBuffers;
		std::vector<std::pair<uint16_t, uint16_t>> g_injectedPortraitDepths;
		uint16_t g_injectedMatchmakingMenuSpriteId = 0;

		void RegisterInjectedPortraitDepth(uint16_t shapeId, uint16_t depth) {
			if (!shapeId) {
				return;
			}
			for (auto& entry : g_injectedPortraitDepths) {
				if (entry.first == shapeId) {
					entry.second = depth;
					return;
				}
			}
			g_injectedPortraitDepths.emplace_back(shapeId, depth);
		}

		bool PathContainsLobby(const std::string& path) {
			std::string lower = path;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			return lower.find("lobby") != std::string::npos;
		}
		bool PathContainsMenu(const std::string& path) {
			std::string lower = path;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			return lower.find("menu") != std::string::npos;
		}

		std::string DiagnosticDumpPath(const std::string& fileName) {
			char modulePath[MAX_PATH] = {};
			GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
			std::string baseDir = modulePath;
			const size_t slash = baseDir.find_last_of("\\/");
			if (slash != std::string::npos) {
				baseDir.resize(slash);
			}
			else {
				baseDir = ".";
			}
			return baseDir + "\\HookCrashersDumps\\" + fileName;
		}

		void DeleteDiagnosticDump(const std::string& fileName) {
			const std::string path = DiagnosticDumpPath(fileName);
			DeleteFileA(path.c_str());
		}

		bool WriteDiagnosticDump(const std::string& fileName, const uint8_t* data, size_t size) {
			if (!data || !size) {
				return false;
			}

			char modulePath[MAX_PATH] = {};
			GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
			std::string baseDir = modulePath;
			const size_t slash = baseDir.find_last_of("\\/");
			if (slash != std::string::npos) {
				baseDir.resize(slash);
			}
			else {
				baseDir = ".";
			}

			const std::string dumpDir = baseDir + "\\HookCrashersDumps";
			CreateDirectoryA(dumpDir.c_str(), nullptr);
			const std::string outPath = dumpDir + "\\" + fileName;

			std::ofstream out(outPath, std::ios::binary);
			if (!out) {
				Util::Logger::Instance().Get()->warn("[SWFInject] failed to open diagnostic dump '{}'.", outPath);
				return false;
			}
			out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
			Util::Logger::Instance().Get()->info(
				"[SWFInject] wrote diagnostic dump '{}' bytes={}.",
				outPath,
				size);
			return true;
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

		uint16_t RemapJpexsTag(uint16_t code) {
			if (code == 148) return 67;
			if (code == 142) return 68;
			return code;
		}

		void AppendPossiblyRemappedTag(std::vector<uint8_t>& out, const Tag& tag) {
			AppendTag(out, RemapJpexsTag(tag.code), tag.payload, tag.length);
		}

		size_t ReadableBytesFrom(const uint8_t* base, size_t maxWanted) {
			if (!base || !maxWanted) {
				return 0;
			}

			size_t readable = 0;
			while (readable < maxWanted) {
				MEMORY_BASIC_INFORMATION mbi{};
				if (!VirtualQuery(base + readable, &mbi, sizeof(mbi))) {
					break;
				}
				const DWORD protect = mbi.Protect & 0xFF;
				const bool committed = mbi.State == MEM_COMMIT;
				const bool readablePage = protect != PAGE_NOACCESS && protect != PAGE_EXECUTE;
				if (!committed || !readablePage || (mbi.Protect & PAGE_GUARD)) {
					break;
				}
				const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
				const uintptr_t cur = reinterpret_cast<uintptr_t>(base + readable);
				if (regionEnd <= cur) {
					break;
				}
				readable += std::min<size_t>(maxWanted - readable, static_cast<size_t>(regionEnd - cur));
			}
			return readable;
		}

		bool TryReadPhysicalTagExtent(const uint8_t* base, size_t readable, size_t offset, uint16_t* codeOut, size_t* endOut) {
			if (!base || offset + 2 > readable) {
				return false;
			}
			const uint8_t* tag = base + offset;
			const uint16_t packed = ReadU16(tag);
			const uint16_t code = packed >> 6;
			uint32_t length = packed & 0x3F;
			const uint8_t* payload = tag + 2;
			if (length == 0x3F) {
				if (offset + 6 > readable) {
					return false;
				}
				length = ReadU32(tag + 2);
				payload = tag + 6;
			}
			const size_t end = static_cast<size_t>((payload - base) + length);
			if (end <= offset || end > readable || end > 64u * 1024u * 1024u) {
				return false;
			}
			if (codeOut) {
				*codeOut = code;
			}
			if (endOut) {
				*endOut = end;
			}
			return true;
		}

		size_t FindNextMenuBitmapCompanionTag(const uint8_t* base, size_t start, size_t readable, uint16_t previousCode) {
			const size_t maxScan = std::min(readable, start + 512u * 1024u);
			for (size_t offset = start + 1; offset + 6 <= maxScan; ++offset) {
				uint16_t code = 0;
				size_t end = 0;
				if (!TryReadPhysicalTagExtent(base, readable, offset, &code, &end)) {
					continue;
				}
				const bool expectedPair = (previousCode == 36 && code == 148) || (previousCode == 148 && code == 36);
				const bool bootstrapPair = (previousCode != 36 && previousCode != 148 && (code == 36 || code == 148));
				if (!expectedPair && !bootstrapPair) {
					continue;
				}
				if (code == 36) {
					const uint16_t characterId = ReadU16(base + offset + (((ReadU16(base + offset) & 0x3F) == 0x3F) ? 6 : 2));
					if (characterId < 700) {
						continue;
					}
				}
				return offset;
			}
			return start;
		}

		size_t ComputeMenuPhysicalCopyLength(const SWFBufferView& view, size_t* tailTagOffsetOut = nullptr) {
			const size_t declaredLength = view.fileLength;
			if (tailTagOffsetOut) {
				*tailTagOffsetOut = declaredLength;
			}

			uint16_t lastDeclaredTagCode = 0;
			const uint8_t* cursor = view.tags;
			const uint8_t* declaredEnd = view.base + declaredLength;
			while (cursor < declaredEnd) {
				Tag tag;
				if (ReadTag(cursor, declaredEnd, tag)) {
					cursor = tag.next;
					lastDeclaredTagCode = tag.code;
					if (tag.code == 0) {
						return declaredLength;
					}
					continue;
				}

				if (cursor + 6 > declaredEnd) {
					break;
				}
				const uint16_t packed = ReadU16(cursor);
				uint32_t length = packed & 0x3F;
				const uint8_t* payload = cursor + 2;
				if (length == 0x3F) {
					length = ReadU32(cursor + 2);
					payload = cursor + 6;
				}

				const size_t requiredLength = static_cast<size_t>((payload - view.base) + length);
				if (requiredLength <= declaredLength || requiredLength > 64u * 1024u * 1024u) {
					break;
				}
				size_t physicalLength = requiredLength;
				const size_t readable = ReadableBytesFrom(view.base, 64u * 1024u * 1024u);
				if (readable >= physicalLength) {
					// menu.cok6 can keep extra physical bitmap records after the declared SWF length.
					// They appear as DefineBitsLossless2(36) records paired with custom 148/142 companion records.
					// Keep the whole physical chain atomic or the loader/decompiler loses sync.
					uint32_t physicalTailTags = 0;
					uint16_t lastPhysicalTailTag = 0;
					for (;;) {
						uint16_t extraCode = 0;
						size_t extraEnd = 0;
						if (!TryReadPhysicalTagExtent(view.base, readable, physicalLength, &extraCode, &extraEnd) || extraCode == 255) {
							const size_t recoveredOffset = FindNextMenuBitmapCompanionTag(view.base, physicalLength, readable, lastPhysicalTailTag);
							if (recoveredOffset == physicalLength) {
								break;
							}
							Util::Logger::Instance().Get()->warn(
								"[SWFInject] menu physical scan recovered 36/148 sequence old_offset={} recovered_offset={} previous_tag={}",
								physicalLength,
								recoveredOffset,
								lastPhysicalTailTag);
							physicalLength = recoveredOffset;
							continue;
						}
						physicalLength = extraEnd;
						++physicalTailTags;
						lastPhysicalTailTag = extraCode;
						if (extraCode == 0) {
							break;
						}
					}
					if (tailTagOffsetOut) {
						*tailTagOffsetOut = static_cast<size_t>(cursor - view.base);
					}
					Util::Logger::Instance().Get()->warn(
						"[SWFInject] menu physical copy length extended declared={} required={} physical={} break_offset={} next_tag={} next_len={} tail_tags={} last_tail_tag={}",
						declaredLength,
						requiredLength,
						physicalLength,
						static_cast<size_t>(cursor - view.base),
						packed >> 6,
						length,
						physicalTailTags,
						lastPhysicalTailTag);
					return physicalLength;
				}
				Util::Logger::Instance().Get()->warn(
					"[SWFInject] menu cannot extend physical copy length declared={} required={} readable={} break_offset={}",
					declaredLength,
					requiredLength,
					readable,
					static_cast<size_t>(cursor - view.base));
				break;
			}

			const size_t readable = ReadableBytesFrom(view.base, 64u * 1024u * 1024u);
			if (readable > declaredLength) {
				size_t physicalLength = declaredLength;
				uint32_t physicalTailTags = 0;
				uint16_t lastPhysicalTailTag = lastDeclaredTagCode;
				for (;;) {
					uint16_t extraCode = 0;
					size_t extraEnd = 0;
					if (!TryReadPhysicalTagExtent(view.base, readable, physicalLength, &extraCode, &extraEnd) || extraCode == 255) {
						const size_t recoveredOffset = FindNextMenuBitmapCompanionTag(view.base, physicalLength, readable, lastPhysicalTailTag);
						if (recoveredOffset == physicalLength) {
							break;
						}
						Util::Logger::Instance().Get()->warn(
							"[SWFInject] menu post-declared scan recovered 36/148 sequence old_offset={} recovered_offset={} previous_tag={}",
							physicalLength,
							recoveredOffset,
							lastPhysicalTailTag);
						physicalLength = recoveredOffset;
						continue;
					}
					const bool expectedPair = (lastPhysicalTailTag == 36 && extraCode == 148) || (lastPhysicalTailTag == 148 && extraCode == 36);
					const bool directBitmapTail = (physicalTailTags == 0 && (extraCode == 36 || extraCode == 148));
					if (!expectedPair && !directBitmapTail) {
						break;
					}
					physicalLength = extraEnd;
					++physicalTailTags;
					lastPhysicalTailTag = extraCode;
				}
				if (physicalLength > declaredLength) {
					if (tailTagOffsetOut) {
						*tailTagOffsetOut = declaredLength;
					}
					Util::Logger::Instance().Get()->warn(
						"[SWFInject] menu physical copy length extended after declared end declared={} physical={} tail_tags={} last_declared_tag={} last_tail_tag={}",
						declaredLength,
						physicalLength,
						physicalTailTags,
						lastDeclaredTagCode,
						lastPhysicalTailTag);
					return physicalLength;
				}
			}
			return declaredLength;
		}
const uint8_t* FindDefineSpriteTagByIdRaw(const uint8_t* cursor, const uint8_t* end, uint16_t spriteId, Tag& tag) {
			if (!cursor || !end || cursor >= end) {
				return nullptr;
			}

			for (const uint8_t* scan = cursor; scan + 6 <= end; ++scan) {
				const uint16_t packed = ReadU16(scan);
				if ((packed >> 6) != 39) {
					continue;
				}

				const uint8_t* payload = scan + 2;
				uint32_t length = packed & 0x3F;
				if (length == 0x3F) {
					if (scan + 6 > end) {
						continue;
					}
					length = ReadU32(scan + 2);
					payload = scan + 6;
				}

				if (length < 4 || payload + length > end) {
					continue;
				}
				if (ReadU16(payload) != spriteId) {
					continue;
				}

				tag.code = 39;
				tag.length = length;
				tag.begin = scan;
				tag.payload = payload;
				tag.next = payload + length;
				return tag.next;
			}

			return nullptr;
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
		std::string HexPreview(const uint8_t* data, size_t length, size_t maxBytes = 32) {
			if (!data || length == 0) {
				return "";
			}
			std::ostringstream stream;
			stream << std::hex << std::uppercase;
			const size_t count = std::min(length, maxBytes);
			for (size_t i = 0; i < count; ++i) {
				if (i) {
					stream << ' ';
				}
				stream.width(2);
				stream.fill('0');
				stream << static_cast<int>(data[i]);
			}
			if (length > maxBytes) {
				stream << " ...";
			}
			return stream.str();
		}

		struct ShapeDiagnostics {
			uint16_t shapeId = 0;
			uint16_t tagCode = 0;
			bool rgbaColors = false;
			RectI32 bounds;
			size_t boundsBytes = 0;
			size_t payloadBytes = 0;
			size_t shapeRecordsBytes = 0;
			uint32_t fillStyles = 0;
			uint32_t lineStyles = 0;
			uint32_t fillBits = 0;
			uint32_t lineBits = 0;
			uint32_t styleChanges = 0;
			uint32_t moveRecords = 0;
			uint32_t fill0Changes = 0;
			uint32_t fill1Changes = 0;
			uint32_t lineChanges = 0;
			uint32_t straightEdges = 0;
			uint32_t curvedEdges = 0;
			uint32_t newStyleRecords = 0;
			uint32_t endRecords = 0;
			bool valid = false;
			std::string error;
		};

		bool DecodeMatrixByteLength(const uint8_t* data, size_t length, size_t& byteLength) {
			if (!data || length == 0) {
				return false;
			}
			size_t bitOffset = 0;
			if (ReadPackedBits(data, bitOffset, 1)) {
				bitOffset += 1;
				if (bitOffset + 5 > length * 8u) return false;
				const uint32_t scaleBits = ReadPackedBits(data, bitOffset, 5);
				bitOffset += 5 + 2u * scaleBits;
			}
			else {
				bitOffset += 1;
			}
			if (bitOffset + 1 > length * 8u) return false;
			if (ReadPackedBits(data, bitOffset, 1)) {
				bitOffset += 1;
				if (bitOffset + 5 > length * 8u) return false;
				const uint32_t rotateBits = ReadPackedBits(data, bitOffset, 5);
				bitOffset += 5 + 2u * rotateBits;
			}
			else {
				bitOffset += 1;
			}
			if (bitOffset + 5 > length * 8u) return false;
			const uint32_t translateBits = ReadPackedBits(data, bitOffset, 5);
			bitOffset += 5 + 2u * translateBits;
			byteLength = (bitOffset + 7u) / 8u;
			return byteLength <= length;
		}

		bool SkipFillStyle(const uint8_t*& cursor, const uint8_t* end, bool rgbaColors) {
			if (cursor >= end) {
				return false;
			}
			const uint8_t type = *cursor++;
			const size_t colorBytes = rgbaColors ? 4u : 3u;
			if (type == 0x00) {
				if (static_cast<size_t>(end - cursor) < colorBytes) return false;
				cursor += colorBytes;
				return true;
			}
			if (type == 0x10 || type == 0x12 || type == 0x13) {
				size_t matrixBytes = 0;
				if (!DecodeMatrixByteLength(cursor, static_cast<size_t>(end - cursor), matrixBytes)) return false;
				cursor += matrixBytes;
				if (cursor >= end) return false;
				uint32_t count = *cursor++;
				if (count == 0xFF) {
					if (end - cursor < 2) return false;
					count = ReadU16(cursor);
					cursor += 2;
				}
				const size_t bytes = static_cast<size_t>(count) * (1u + colorBytes);
				if (static_cast<size_t>(end - cursor) < bytes) return false;
				cursor += bytes;
				return true;
			}
			if (type == 0x40 || type == 0x41 || type == 0x42 || type == 0x43) {
				if (end - cursor < 2) return false;
				cursor += 2;
				size_t matrixBytes = 0;
				if (!DecodeMatrixByteLength(cursor, static_cast<size_t>(end - cursor), matrixBytes)) return false;
				cursor += matrixBytes;
				return true;
			}
			return false;
		}

		bool AnalyzeDefineShapePayload(const std::vector<uint8_t>& payload, uint16_t tagCode, ShapeDiagnostics& diag) {
			diag = ShapeDiagnostics{};
			diag.payloadBytes = payload.size();
			diag.tagCode = tagCode;
			diag.rgbaColors = tagCode >= 32;
			if (payload.size() < 4) {
				diag.error = "payload too small";
				return false;
			}
			diag.shapeId = ReadU16(payload.data());
			size_t rectBytes = 0;
			if (!DecodeRect(payload.data() + 2, payload.size() - 2, diag.bounds, rectBytes)) {
				diag.error = "invalid bounds rect";
				return false;
			}
			diag.boundsBytes = rectBytes;
			const uint8_t* cursor = payload.data() + 2 + rectBytes;
			const uint8_t* end = payload.data() + payload.size();
			if (cursor >= end) { diag.error = "missing fill style count"; return false; }
			diag.fillStyles = *cursor++;
			if (diag.fillStyles == 0xFF) {
				if (end - cursor < 2) { diag.error = "truncated extended fill count"; return false; }
				diag.fillStyles = ReadU16(cursor);
				cursor += 2;
			}
			for (uint32_t i = 0; i < diag.fillStyles; ++i) {
				if (!SkipFillStyle(cursor, end, diag.rgbaColors)) {
					diag.error = "truncated/unknown fill style";
					return false;
				}
			}
			if (cursor >= end) { diag.error = "missing line style count"; return false; }
			diag.lineStyles = *cursor++;
			if (diag.lineStyles == 0xFF) {
				if (end - cursor < 2) { diag.error = "truncated extended line count"; return false; }
				diag.lineStyles = ReadU16(cursor);
				cursor += 2;
			}
			const size_t lineBytes = static_cast<size_t>(diag.lineStyles) * (2u + (diag.rgbaColors ? 4u : 3u));
			if (static_cast<size_t>(end - cursor) < lineBytes) { diag.error = "truncated line styles"; return false; }
			cursor += lineBytes;
			if (cursor >= end) { diag.error = "missing shape record bits"; return false; }
			diag.shapeRecordsBytes = static_cast<size_t>(end - cursor);
			size_t bitOffset = 0;
			diag.fillBits = ReadPackedBits(cursor, bitOffset, 4);
			bitOffset += 4;
			diag.lineBits = ReadPackedBits(cursor, bitOffset, 4);
			bitOffset += 4;
			const size_t recordBitsLimit = diag.shapeRecordsBytes * 8u;
			while (bitOffset + 6 <= recordBitsLimit) {
				const uint32_t typeFlag = ReadPackedBits(cursor, bitOffset, 1);
				bitOffset += 1;
				if (typeFlag) {
					if (bitOffset + 1 > recordBitsLimit) break;
					const uint32_t straightFlag = ReadPackedBits(cursor, bitOffset, 1);
					bitOffset += 1;
					if (straightFlag) {
						++diag.straightEdges;
						if (bitOffset + 5 > recordBitsLimit) break;
						const uint32_t nbits = ReadPackedBits(cursor, bitOffset, 4) + 2;
						bitOffset += 4;
						const uint32_t generalLine = ReadPackedBits(cursor, bitOffset, 1);
						bitOffset += 1;
						const size_t edgeBits = generalLine ? 2u * nbits : 1u + nbits;
						if (bitOffset + edgeBits > recordBitsLimit) break;
						bitOffset += edgeBits;
					}
					else {
						++diag.curvedEdges;
						if (bitOffset + 4 > recordBitsLimit) break;
						const uint32_t nbits = ReadPackedBits(cursor, bitOffset, 4) + 2;
						if (bitOffset + 4u + 4u * nbits > recordBitsLimit) break;
						bitOffset += 4 + 4u * nbits;
					}
					continue;
				}
				if (bitOffset + 5 > recordBitsLimit) break;
				const uint32_t flags = ReadPackedBits(cursor, bitOffset, 5);
				bitOffset += 5;
				if (flags == 0) { ++diag.endRecords; break; }
				++diag.styleChanges;
				if (flags & 0x01) {
					++diag.moveRecords;
					if (bitOffset + 5 > recordBitsLimit) break;
					const uint32_t moveBits = ReadPackedBits(cursor, bitOffset, 5);
					if (bitOffset + 5u + 2u * moveBits > recordBitsLimit) break;
					bitOffset += 5 + 2u * moveBits;
				}
				if (flags & 0x02) { if (bitOffset + diag.fillBits > recordBitsLimit) break; ++diag.fill0Changes; bitOffset += diag.fillBits; }
				if (flags & 0x04) { if (bitOffset + diag.fillBits > recordBitsLimit) break; ++diag.fill1Changes; bitOffset += diag.fillBits; }
				if (flags & 0x08) { if (bitOffset + diag.lineBits > recordBitsLimit) break; ++diag.lineChanges; bitOffset += diag.lineBits; }
				if (flags & 0x10) { ++diag.newStyleRecords; break; }
			}
			diag.valid = true;
			return true;
		}

		void LogShapeDiagnostics(const char* label, const std::vector<uint8_t>& payload, uint16_t tagCode) {
			ShapeDiagnostics diag;
			AnalyzeDefineShapePayload(payload, tagCode, diag);
			Util::Logger::Instance().Get()->debug(
				"[SWFInject][ShapeDiag] {} valid={} tag={} rgba_colors={} error='{}' shape_id={} payload_bytes={} bounds=[{},{} -> {},{}] bounds_bytes={} fill_styles={} line_styles={} fill_bits={} line_bits={} records_bytes={} style_changes={} moves={} fill0={} fill1={} lines={} straight_edges={} curved_edges={} new_styles={} ends={} head=[{}].",
				label ? label : "shape",
				diag.valid,
				diag.tagCode,
				diag.rgbaColors,
				diag.error,
				diag.shapeId,
				diag.payloadBytes,
				diag.bounds.xmin,
				diag.bounds.ymin,
				diag.bounds.xmax,
				diag.bounds.ymax,
				diag.boundsBytes,
				diag.fillStyles,
				diag.lineStyles,
				diag.fillBits,
				diag.lineBits,
				diag.shapeRecordsBytes,
				diag.styleChanges,
				diag.moveRecords,
				diag.fill0Changes,
				diag.fill1Changes,
				diag.lineChanges,
				diag.straightEdges,
				diag.curvedEdges,
				diag.newStyleRecords,
				diag.endRecords,
				HexPreview(payload.data(), payload.size(), 48));
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

		void PutStraightEdge(std::vector<uint8_t>& bits, int32_t dx, int32_t dy);
		void PutCurvedEdge(std::vector<uint8_t>& bits, int32_t controlDx, int32_t controlDy, int32_t anchorDx, int32_t anchorDy);

		bool FindShapeRecordsStart(const std::vector<uint8_t>& payload, uint16_t tagCode, size_t& rectBytes, size_t& recordsOffset) {
			if (payload.size() < 4) {
				return false;
			}
			RectI32 bounds{};
			if (!DecodeRect(payload.data() + 2, payload.size() - 2, bounds, rectBytes)) {
				return false;
			}
			const bool rgbaColors = tagCode >= 32;
			const uint8_t* begin = payload.data();
			const uint8_t* cursor = begin + 2 + rectBytes;
			const uint8_t* end = begin + payload.size();
			if (cursor >= end) {
				return false;
			}
			uint32_t fillCount = *cursor++;
			if (fillCount == 0xFF) {
				if (end - cursor < 2) return false;
				fillCount = ReadU16(cursor);
				cursor += 2;
			}
			for (uint32_t i = 0; i < fillCount; ++i) {
				if (!SkipFillStyle(cursor, end, rgbaColors)) {
					return false;
				}
			}
			if (cursor >= end) {
				return false;
			}
			uint32_t lineCount = *cursor++;
			if (lineCount == 0xFF) {
				if (end - cursor < 2) return false;
				lineCount = ReadU16(cursor);
				cursor += 2;
			}
			const size_t lineBytes = static_cast<size_t>(lineCount) * (2u + (rgbaColors ? 4u : 3u));
			if (static_cast<size_t>(end - cursor) < lineBytes) {
				return false;
			}
			cursor += lineBytes;
			if (cursor >= end) {
				return false;
			}
			recordsOffset = static_cast<size_t>(cursor - begin);
			return true;
		}

		void PutStyleChangeRecord(std::vector<uint8_t>& bits, uint32_t flags, int32_t moveX, int32_t moveY, uint32_t fill0, uint32_t fill1, uint32_t line, uint32_t fillBits, uint32_t lineBits) {
			PutBits(bits, 0, 1);
			PutBits(bits, flags, 5);
			if (flags & 0x01) {
				const int moveBits = SignedBitCount({ moveX, moveY });
				PutBits(bits, static_cast<uint32_t>(moveBits), 5);
				PutSignedBits(bits, moveX, moveBits);
				PutSignedBits(bits, moveY, moveBits);
			}
			if (flags & 0x02) PutBits(bits, fill0, static_cast<int>(fillBits));
			if (flags & 0x04) PutBits(bits, fill1, static_cast<int>(fillBits));
			if (flags & 0x08) PutBits(bits, line, static_cast<int>(lineBits));
		}

		bool ScaleDefineShapePayloadToBounds(const std::vector<uint8_t>& payload, uint16_t tagCode, const RectI32& targetBounds, const std::vector<uint8_t>& targetBoundsBytes, std::vector<uint8_t>& out) {
			out.clear();
			if (payload.size() < 4 || targetBoundsBytes.empty()) {
				return false;
			}
			RectI32 sourceBounds{};
			size_t sourceBoundsBytes = 0;
			if (!DecodeRect(payload.data() + 2, payload.size() - 2, sourceBounds, sourceBoundsBytes)) {
				return false;
			}
			size_t recordsOffset = 0;
			if (!FindShapeRecordsStart(payload, tagCode, sourceBoundsBytes, recordsOffset)) {
				return false;
			}
			const int32_t sourceW = std::max<int32_t>(1, sourceBounds.xmax - sourceBounds.xmin);
			const int32_t sourceH = std::max<int32_t>(1, sourceBounds.ymax - sourceBounds.ymin);
			const int32_t targetW = std::max<int32_t>(1, targetBounds.xmax - targetBounds.xmin);
			const int32_t targetH = std::max<int32_t>(1, targetBounds.ymax - targetBounds.ymin);
			const double scaleX = static_cast<double>(targetW) / static_cast<double>(sourceW);
			const double scaleY = static_cast<double>(targetH) / static_cast<double>(sourceH);
			auto mapX = [&](int32_t x) -> int32_t {
				return static_cast<int32_t>(std::lround(static_cast<double>(targetBounds.xmin) + static_cast<double>(x - sourceBounds.xmin) * scaleX));
			};
			auto mapY = [&](int32_t y) -> int32_t {
				return static_cast<int32_t>(std::lround(static_cast<double>(targetBounds.ymin) + static_cast<double>(y - sourceBounds.ymin) * scaleY));
			};

			const uint8_t* records = payload.data() + recordsOffset;
			const size_t recordsBytes = payload.size() - recordsOffset;
			const size_t recordBitsLimit = recordsBytes * 8u;
			size_t bitOffset = 0;
			if (recordBitsLimit < 8) {
				return false;
			}
			uint32_t fillBits = ReadPackedBits(records, bitOffset, 4);
			bitOffset += 4;
			uint32_t lineBits = ReadPackedBits(records, bitOffset, 4);
			bitOffset += 4;
			std::vector<uint8_t> bits;
			PutBits(bits, fillBits, 4);
			PutBits(bits, lineBits, 4);

			int32_t sourceX = 0;
			int32_t sourceY = 0;
			int32_t scaledX = mapX(0);
			int32_t scaledY = mapY(0);
			bool sawEnd = false;
			while (bitOffset + 6 <= recordBitsLimit) {
				const uint32_t typeFlag = ReadPackedBits(records, bitOffset, 1);
				bitOffset += 1;
				if (typeFlag) {
					if (bitOffset + 1 > recordBitsLimit) return false;
					const uint32_t straightFlag = ReadPackedBits(records, bitOffset, 1);
					bitOffset += 1;
					if (straightFlag) {
						if (bitOffset + 5 > recordBitsLimit) return false;
						const uint32_t nbits = ReadPackedBits(records, bitOffset, 4) + 2;
						bitOffset += 4;
						const uint32_t generalLine = ReadPackedBits(records, bitOffset, 1);
						bitOffset += 1;
						int32_t dx = 0;
						int32_t dy = 0;
						if (generalLine) {
							if (bitOffset + 2u * nbits > recordBitsLimit) return false;
							dx = ReadPackedSignedBits(records, bitOffset, static_cast<int>(nbits));
							dy = ReadPackedSignedBits(records, bitOffset, static_cast<int>(nbits));
						}
						else {
							if (bitOffset + 1u + nbits > recordBitsLimit) return false;
							const uint32_t verticalLine = ReadPackedBits(records, bitOffset, 1);
							bitOffset += 1;
							if (verticalLine) dy = ReadPackedSignedBits(records, bitOffset, static_cast<int>(nbits));
							else dx = ReadPackedSignedBits(records, bitOffset, static_cast<int>(nbits));
						}
						sourceX += dx;
						sourceY += dy;
						const int32_t nextScaledX = mapX(sourceX);
						const int32_t nextScaledY = mapY(sourceY);
						PutStraightEdge(bits, nextScaledX - scaledX, nextScaledY - scaledY);
						scaledX = nextScaledX;
						scaledY = nextScaledY;
					}
					else {
						if (bitOffset + 4 > recordBitsLimit) return false;
						const uint32_t nbits = ReadPackedBits(records, bitOffset, 4) + 2;
						bitOffset += 4;
						if (bitOffset + 4u * nbits > recordBitsLimit) return false;
						const int32_t controlDx = ReadPackedSignedBits(records, bitOffset, static_cast<int>(nbits));
						const int32_t controlDy = ReadPackedSignedBits(records, bitOffset, static_cast<int>(nbits));
						const int32_t anchorDx = ReadPackedSignedBits(records, bitOffset, static_cast<int>(nbits));
						const int32_t anchorDy = ReadPackedSignedBits(records, bitOffset, static_cast<int>(nbits));
						const int32_t controlX = sourceX + controlDx;
						const int32_t controlY = sourceY + controlDy;
						const int32_t anchorX = controlX + anchorDx;
						const int32_t anchorY = controlY + anchorDy;
						const int32_t scaledControlX = mapX(controlX);
						const int32_t scaledControlY = mapY(controlY);
						const int32_t scaledAnchorX = mapX(anchorX);
						const int32_t scaledAnchorY = mapY(anchorY);
						PutCurvedEdge(bits, scaledControlX - scaledX, scaledControlY - scaledY, scaledAnchorX - scaledControlX, scaledAnchorY - scaledControlY);
						sourceX = anchorX;
						sourceY = anchorY;
						scaledX = scaledAnchorX;
						scaledY = scaledAnchorY;
					}
					continue;
				}

				if (bitOffset + 5 > recordBitsLimit) return false;
				const uint32_t flags = ReadPackedBits(records, bitOffset, 5);
				bitOffset += 5;
				if (flags == 0) {
					PutBits(bits, 0, 1);
					PutBits(bits, 0, 5);
					sawEnd = true;
					break;
				}
				if (flags & 0x10) {
					Util::Logger::Instance().Get()->warn("[SWFInject] imported SWF shape uses new-style records; skip geometric scaling.");
					return false;
				}
				int32_t moveX = 0;
				int32_t moveY = 0;
				uint32_t fill0 = 0;
				uint32_t fill1 = 0;
				uint32_t line = 0;
				if (flags & 0x01) {
					if (bitOffset + 5 > recordBitsLimit) return false;
					const uint32_t moveBits = ReadPackedBits(records, bitOffset, 5);
					bitOffset += 5;
					if (moveBits == 0 || bitOffset + 2u * moveBits > recordBitsLimit) return false;
					sourceX = ReadPackedSignedBits(records, bitOffset, static_cast<int>(moveBits));
					sourceY = ReadPackedSignedBits(records, bitOffset, static_cast<int>(moveBits));
					scaledX = mapX(sourceX);
					scaledY = mapY(sourceY);
					moveX = scaledX;
					moveY = scaledY;
				}
				if (flags & 0x02) {
					if (bitOffset + fillBits > recordBitsLimit) return false;
					fill0 = ReadPackedBits(records, bitOffset, static_cast<int>(fillBits));
					bitOffset += fillBits;
				}
				if (flags & 0x04) {
					if (bitOffset + fillBits > recordBitsLimit) return false;
					fill1 = ReadPackedBits(records, bitOffset, static_cast<int>(fillBits));
					bitOffset += fillBits;
				}
				if (flags & 0x08) {
					if (bitOffset + lineBits > recordBitsLimit) return false;
					line = ReadPackedBits(records, bitOffset, static_cast<int>(lineBits));
					bitOffset += lineBits;
				}
				PutStyleChangeRecord(bits, flags, moveX, moveY, fill0, fill1, line, fillBits, lineBits);
			}
			if (!sawEnd) {
				return false;
			}

			out.reserve(payload.size());
			out.insert(out.end(), payload.begin(), payload.begin() + 2);
			out.insert(out.end(), targetBoundsBytes.begin(), targetBoundsBytes.end());
			out.insert(out.end(), payload.begin() + 2 + sourceBoundsBytes, payload.begin() + recordsOffset);
			const std::vector<uint8_t> packedRecords = PackBits(bits);
			out.insert(out.end(), packedRecords.begin(), packedRecords.end());
			Util::Logger::Instance().Get()->debug(
				"[SWFInject] scaled imported shape id={} tag={} source_bounds=[{},{} -> {},{}] target_bounds=[{},{} -> {},{}] scale=({}, {}) old_bytes={} new_bytes={}",
				ReadU16(payload.data()),
				tagCode,
				sourceBounds.xmin,
				sourceBounds.ymin,
				sourceBounds.xmax,
				sourceBounds.ymax,
				targetBounds.xmin,
				targetBounds.ymin,
				targetBounds.xmax,
				targetBounds.ymax,
				scaleX,
				scaleY,
				payload.size(),
				out.size());
			return true;
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

		void PutCurvedEdge(std::vector<uint8_t>& bits, int32_t controlDx, int32_t controlDy, int32_t anchorDx, int32_t anchorDy) {
			PutBits(bits, 1, 1);
			PutBits(bits, 0, 1);
			const int n = std::max(2, SignedBitCount({ controlDx, controlDy, anchorDx, anchorDy }));
			PutBits(bits, static_cast<uint32_t>(n - 2), 4);
			PutSignedBits(bits, controlDx, n);
			PutSignedBits(bits, controlDy, n);
			PutSignedBits(bits, anchorDx, n);
			PutSignedBits(bits, anchorDy, n);
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
			Util::Logger::Instance().Get()->debug(
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
			PutBits(bits, 0, 1); // StyleChangeRecord
			PutBits(bits, 0, 1); // NewStyles
			PutBits(bits, 0, 1); // LineStyle
			PutBits(bits, 0, 1); // FillStyle1 = false
			PutBits(bits, 1, 1); // FillStyle0 = true
			PutBits(bits, 1, 1); // MoveTo

			const int moveBits = SignedBitCount({ x, y });
			PutBits(bits, static_cast<uint32_t>(moveBits), 5);
			PutSignedBits(bits, x, moveBits);
			PutSignedBits(bits, y, moveBits);

			PutBits(bits, fillIndex, fillBits);
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
			Util::Logger::Instance().Get()->debug(
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

		std::vector<uint8_t> ReadBinaryFile(const std::string& path) {
			std::ifstream file(path, std::ios::binary);
			if (!file) return {};
			return std::vector<uint8_t>(
				std::istreambuf_iterator<char>(file),
				std::istreambuf_iterator<char>()
			);
		}

		bool ExtractFirstDefineShapeFromSwf(
			const std::string& path,
			uint16_t newShapeId,
			std::vector<uint8_t>& outPayload,
			uint16_t& outTagCode
		) {
			const std::vector<uint8_t> swf = ReadBinaryFile(path);

			if (!FileExistsA(path)) {
				Util::Logger::Instance().Get()->warn(
					"[SWFInject] file does not exist '{}'.",
					path
				);
				return false;
			}

			if (swf.size() < 8 || swf[1] != 'W' || swf[2] != 'S') {
				Util::Logger::Instance().Get()->warn(
					"[SWFInject] '{}' is not a SWF.",
					path
				);
				return false;
			}

			if (swf[0] != 'F') {
				Util::Logger::Instance().Get()->warn(
					"[SWFInject] '{}' is compressed (%cWS). Export as FWS.",
					path,
					swf[0]
				);
				return false;
			}

			size_t rectBytes = 0;
			RectI32 dummy{};
			if (!DecodeRect(swf.data() + 8, swf.size() - 8, dummy, rectBytes)) {
				Util::Logger::Instance().Get()->warn("[SWFInject] failed to decode SWF header RECT from '{}'.", path);
				return false;
			}

			const uint8_t* cursor = swf.data() + 8 + rectBytes + 4; // RECT + FPS + FrameCount
			const uint8_t* end = swf.data() + swf.size();

			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) break;
				Util::Logger::Instance().Get()->debug(
					"[SWFInject] tag={} len={}",
					tag.code,
					tag.length
				);
				if ((tag.code == 32 || tag.code == 83 || tag.code == 22 || tag.code == 2) && tag.length >= 2) {
					outPayload.assign(tag.payload, tag.payload + tag.length);
					PatchU16(outPayload, 0, newShapeId);
					outTagCode = tag.code;

					Util::Logger::Instance().Get()->debug(
						"[SWFInject] imported DefineShape tag={} bytes={} old_id={} new_id={} from '{}'.",
						tag.code,
						tag.length,
						ReadU16(tag.payload),
						newShapeId,
						path
					);
					return true;
				}

				if (tag.code == 0) break;
				cursor = tag.next;
			}

			Util::Logger::Instance().Get()->warn("[SWFInject] no DefineShape found in '{}'.", path);
			return false;
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

		std::vector<SvgPathSegment> ApplySvgTransform(const SvgTransform& transform, const std::vector<SvgPathSegment>& segments) {
			std::vector<SvgPathSegment> out;
			out.reserve(segments.size());
			for (const SvgPathSegment& segment : segments) {
				out.push_back({ segment.curved, ApplySvgTransform(transform, segment.control), ApplySvgTransform(transform, segment.anchor) });
			}
			return out;
		}

		void AddSvgShape(SvgDocument& doc, const ColorRgba& fill, const std::vector<SvgPoint>& points, size_t pathGroup = 0, const std::vector<SvgPathSegment>& segments = {}) {
			if (fill.a == 0 || points.size() < 3) {
				return;
			}
			doc.shapes.push_back({ fill, points, segments, pathGroup });
		}

		void ParseSvgPathData(SvgDocument& doc, const std::string& d, const ColorRgba& fill, const SvgTransform& transform, size_t pathGroup) {
			static constexpr int kSvgCurveSegments = 4;
			size_t i = 0;
			char cmd = 0;
			SvgPoint cur{};
			SvgPoint start{};
			std::vector<SvgPoint> poly;
			std::vector<SvgPathSegment> segments;
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
					AddSvgShape(doc, fill, ApplySvgTransform(transform, poly), pathGroup, ApplySvgTransform(transform, segments));
				}
				poly.clear();
				segments.clear();
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

					if (!poly.empty()) {
						if (cur.x != start.x || cur.y != start.y) {
							segments.push_back({ false, {}, start });
							poly.push_back(start);
						}
						closePoly();
					}

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
						const SvgPoint next{ x, y };
						segments.push_back({ false, {}, next });
						cur = next;
						poly.push_back(cur);
					}
				}
				else if (op == 'H') {
					while (hasNumber()) {
						float x = read();
						if (rel) x += cur.x;
						const SvgPoint next{ x, cur.y };
						segments.push_back({ false, {}, next });
						cur = next;
						poly.push_back(cur);
					}
				}
				else if (op == 'V') {
					while (hasNumber()) {
						float y = read();
						if (rel) y += cur.y;
						const SvgPoint next{ cur.x, y };
						segments.push_back({ false, {}, next });
						cur = next;
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
						for (int step = 1; step <= kSvgCurveSegments; ++step) {
							const float t = static_cast<float>(step) / static_cast<float>(kSvgCurveSegments);
							const float u = 1.0f - t;
							const SvgPoint next{
								u * u * u * p0.x + 3.0f * u * u * t * c1.x + 3.0f * u * t * t * c2.x + t * t * t * end.x,
								u * u * u * p0.y + 3.0f * u * u * t * c1.y + 3.0f * u * t * t * c2.y + t * t * t * end.y
								};
							segments.push_back({ false, {}, next });
							poly.push_back(next);
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
						segments.push_back({ true, c, end });
						for (int step = 1; step <= kSvgCurveSegments; ++step) {
							const float t = static_cast<float>(step) / static_cast<float>(kSvgCurveSegments);
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
					if (cur.x != start.x || cur.y != start.y) {
						segments.push_back({ false, {}, start });
					}
					poly.push_back(start);
					closePoly();
				}
				else {
					Util::Logger::Instance().Get()->warn("[SWFInject] unsupported SVG path command '{}'; partial path converted.", cmd);
					break;
				}
			}
			if (!poly.empty()) {
				if (cur.x != start.x || cur.y != start.y) {
					segments.push_back({ false, {}, start });
					poly.push_back(start);
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
			SvgTransform inheritedTransform;
			bool hasInheritedTransform = false;
			const size_t groupStart = text.find("<g");
			if (groupStart != std::string::npos) {
				const size_t groupEnd = text.find('>', groupStart);
				const size_t groupClose = groupEnd == std::string::npos ? std::string::npos : text.find("</g>", groupEnd);
				if (groupEnd != std::string::npos && groupClose != std::string::npos) {
					const std::string groupTag = text.substr(groupStart, groupEnd - groupStart + 1);
					const std::string groupTransform = SvgAttribute(groupTag, "transform");
					if (!groupTransform.empty()) {
						inheritedTransform = ParseSvgTransform(groupTransform);
						hasInheritedTransform = true;
						Util::Logger::Instance().Get()->debug(
							"[SWFInject] applying root SVG group transform for portrait '{}': '{}'.",
							path,
							groupTransform);
					}
				}
			}
			if (!hasInheritedTransform && text.find("<g") != std::string::npos && text.find("transform=") != std::string::npos) {
				Util::Logger::Instance().Get()->warn(
					"[SWFInject] SVG portrait '{}' contains group transforms, but no supported root group transform was applied.",
					path);
			}
			auto combineTransform = [&](const std::string& elementTransform) {
				return MultiplySvgTransform(inheritedTransform, ParseSvgTransform(elementTransform));
				};

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

			size_t nextPathGroup = 1;
			forEachTag("path", [&](const std::string& tag) {
				ParseSvgPathData(doc, SvgAttribute(tag, "d"), ParseSvgFill(tag), combineTransform(SvgAttribute(tag, "transform")), nextPathGroup++);
				});
			forEachTag("polygon", [&](const std::string& tag) {
				const ColorRgba fill = ParseSvgFill(tag);
				const SvgTransform transform = combineTransform(SvgAttribute(tag, "transform"));
				const std::vector<float> n = ParseSvgNumbers(SvgAttribute(tag, "points"));
				std::vector<SvgPoint> points;
				for (size_t i = 0; i + 1 < n.size(); i += 2) {
					points.push_back({ n[i], n[i + 1] });
				}
				AddSvgShape(doc, fill, ApplySvgTransform(transform, static_cast<const std::vector<SvgPoint>&>(points)));
				});
			forEachTag("polyline", [&](const std::string& tag) {
				const ColorRgba fill = ParseSvgFill(tag);
				const SvgTransform transform = combineTransform(SvgAttribute(tag, "transform"));
				const std::vector<float> n = ParseSvgNumbers(SvgAttribute(tag, "points"));
				std::vector<SvgPoint> points;
				for (size_t i = 0; i + 1 < n.size(); i += 2) {
					points.push_back({ n[i], n[i + 1] });
				}
				AddSvgShape(doc, fill, ApplySvgTransform(transform, static_cast<const std::vector<SvgPoint>&>(points)));
				});
			forEachTag("rect", [&](const std::string& tag) {
				const float x = ParseSvgFloat(SvgAttribute(tag, "x"));
				const float y = ParseSvgFloat(SvgAttribute(tag, "y"));
				const float w = ParseSvgFloat(SvgAttribute(tag, "width"));
				const float h = ParseSvgFloat(SvgAttribute(tag, "height"));
				const SvgTransform transform = combineTransform(SvgAttribute(tag, "transform"));
				AddSvgShape(doc, ParseSvgFill(tag), ApplySvgTransform(transform, std::vector<SvgPoint>{ {x, y}, { x + w, y }, { x + w, y + h }, { x, y + h } }));
				});
			auto ellipseFn = [&](const std::string& tag, bool circle) {
				const float cx = ParseSvgFloat(SvgAttribute(tag, "cx"));
				const float cy = ParseSvgFloat(SvgAttribute(tag, "cy"));
				const float rx = circle ? ParseSvgFloat(SvgAttribute(tag, "r")) : ParseSvgFloat(SvgAttribute(tag, "rx"));
				const float ry = circle ? rx : ParseSvgFloat(SvgAttribute(tag, "ry"));
				const SvgTransform transform = combineTransform(SvgAttribute(tag, "transform"));
				std::vector<SvgPoint> points;
				for (int i = 0; i < 32; ++i) {
					const float a = static_cast<float>(i) * 6.28318530718f / 32.0f;
					points.push_back({ cx + std::cos(a) * rx, cy + std::sin(a) * ry });
				}
				AddSvgShape(doc, ParseSvgFill(tag), ApplySvgTransform(transform, points));
				};
			forEachTag("circle", [&](const std::string& tag) { ellipseFn(tag, true); });
			forEachTag("ellipse", [&](const std::string& tag) { ellipseFn(tag, false); });

			Util::Logger::Instance().Get()->debug(
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

		void MarkKnownCharacterIds(const uint8_t* tags, const uint8_t* end, std::array<bool, 65536>& usedIds) {
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
					usedIds[ReadU16(tag.payload)] = true;
				}
				else if ((tag.code == 56 || tag.code == 76) && tag.length >= 2) { // ExportAssets / SymbolClass
					const uint8_t* payload = tag.payload;
					const uint8_t* payloadEnd = tag.payload + tag.length;
					const uint16_t count = ReadU16(payload);
					payload += 2;
					for (uint16_t i = 0; i < count && payload + 2 <= payloadEnd; ++i) {
						usedIds[ReadU16(payload)] = true;
						payload += 2;
						while (payload < payloadEnd && *payload != 0) {
							++payload;
						}
						if (payload < payloadEnd) {
							++payload;
						}
					}
				}
				cursor = tag.next;
			}
		}

		void MarkKnownCharacterIdsPhysicalSequence(const uint8_t* tags, const uint8_t* end, std::array<bool, 65536>& usedIds) {
			const uint8_t* cursor = tags;
			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					break;
				}
				if (IsCharacterIdTag(tag.code) && tag.length >= 2) {
					usedIds[ReadU16(tag.payload)] = true;
				}
				else if ((tag.code == 56 || tag.code == 76) && tag.length >= 2) {
					const uint8_t* payload = tag.payload;
					const uint8_t* payloadEnd = tag.payload + tag.length;
					const uint16_t count = ReadU16(payload);
					payload += 2;
					if (count <= 512) {
						for (uint16_t i = 0; i < count && payload + 2 <= payloadEnd; ++i) {
							usedIds[ReadU16(payload)] = true;
							payload += 2;
							while (payload < payloadEnd && *payload != 0) {
								++payload;
							}
							if (payload < payloadEnd) {
								++payload;
							}
						}
					}
				}
				cursor = tag.next;
			}
		}

		void MarkKnownCharacterIdsRaw(const uint8_t* begin, const uint8_t* end, std::array<bool, 65536>& usedIds) {
			if (!begin || !end || begin >= end) {
				return;
			}

			for (const uint8_t* scan = begin; scan + 4 <= end; ++scan) {
				const uint16_t packed = ReadU16(scan);
				const uint16_t code = packed >> 6;

				const uint8_t* payload = scan + 2;
				uint32_t length = packed & 0x3F;
				if (length == 0x3F) {
					if (scan + 6 > end) {
						continue;
					}
					length = ReadU32(scan + 2);
					payload = scan + 6;
				}
				if (length < 2 || payload + length > end) {
					continue;
				}

				const uint16_t firstPayloadId = ReadU16(payload);
				if (firstPayloadId > 0 && firstPayloadId <= 2048) {
					usedIds[firstPayloadId] = true;
				}

				if (!IsCharacterIdTag(code) && code != 56 && code != 76) {
					continue;
				}

				if (IsCharacterIdTag(code)) {
					if (firstPayloadId > 0 && firstPayloadId <= 2048) {
						usedIds[firstPayloadId] = true;
					}
					continue;
				}

				const uint8_t* payloadEnd = payload + length;
				const uint16_t count = ReadU16(payload);
				payload += 2;
				if (count > 512) {
					continue;
				}
				for (uint16_t i = 0; i < count && payload + 2 <= payloadEnd; ++i) {
					usedIds[ReadU16(payload)] = true;
					payload += 2;
					while (payload < payloadEnd && *payload != 0) {
						++payload;
					}
					if (payload < payloadEnd) {
						++payload;
					}
				}
			}
		}

		uint16_t MaxCharacterId(const uint8_t* tags, const uint8_t* end) {
			std::array<bool, 65536> usedIds{};
			MarkKnownCharacterIds(tags, end, usedIds);
			uint16_t maxId = 0;
			for (uint32_t id = 1; id < usedIds.size(); ++id) {
				if (usedIds[id]) {
					maxId = static_cast<uint16_t>(id);
				}
			}
			return maxId;
		}

		uint16_t AllocateCharacterId(std::array<bool, 65536>& usedIds, uint16_t& nextId) {
			while (nextId < 65534 && usedIds[nextId]) {
				++nextId;
			}
			if (nextId >= 65534) {
				return 0;
			}
			const uint16_t allocated = nextId++;
			usedIds[allocated] = true;
			return allocated;
		}

		uint16_t MaxDefineShapeId(const uint8_t* tags, const uint8_t* end) {
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
				if ((tag.code == 2 || tag.code == 22 || tag.code == 32 || tag.code == 83) && tag.length >= 2) {
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

		std::vector<uint8_t> FindShapePayload(const SWFBufferView& view, uint16_t shapeId, uint16_t* tagCodeOut = nullptr) {
			uint16_t lastDeclaredTagCode = 0;
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
					if (tagCodeOut) {
						*tagCodeOut = tag.code;
					}
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
			out.push_back(0x40);
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
			Util::Logger::Instance().Get()->debug(
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
			}
			out.push_back(0);

			std::vector<uint8_t> bits;
			const int fillBits = 6;
			PutBits(bits, static_cast<uint32_t>(fillBits), 4);
			PutBits(bits, 0, 4);
			struct EncodedSegment {
				bool curved = false;
				std::pair<int32_t, int32_t> control{};
				std::pair<int32_t, int32_t> anchor{};
			};
			struct EncodedContour {
				size_t shapeIndex = 0;
				size_t pathGroup = 0;
				int64_t signedArea2 = 0;
				uint64_t absArea2 = 0;
				std::vector<std::pair<int32_t, int32_t>> points;
				std::vector<EncodedSegment> segments;
			};
			std::vector<EncodedContour> contours;
			contours.reserve(portrait.svg.shapes.size());
			for (size_t i = 0; i < portrait.svg.shapes.size(); ++i) {
				const SvgShape& shape = portrait.svg.shapes[i];
				if (shape.points.size() < 3) {
					continue;
				}
				EncodedContour contour;
				contour.shapeIndex = i;
				contour.pathGroup = shape.pathGroup;
				contour.points.reserve(shape.points.size());
				for (const SvgPoint& point : shape.points) {
					const auto mapped = mapPoint(point);
					if (contour.points.empty() || contour.points.back() != mapped) {
						contour.points.push_back(mapped);
					}
				}
				contour.segments.reserve(shape.segments.size());
				for (const SvgPathSegment& segment : shape.segments) {
					contour.segments.push_back({ segment.curved, mapPoint(segment.control), mapPoint(segment.anchor) });
				}
				if (contour.points.size() < 3) {
					continue;
				}
				for (size_t p = 0; p < contour.points.size(); ++p) {
					const auto& a = contour.points[p];
					const auto& b = contour.points[(p + 1) % contour.points.size()];
					contour.signedArea2 += static_cast<int64_t>(a.first) * b.second - static_cast<int64_t>(b.first) * a.second;
				}
				contour.absArea2 = static_cast<uint64_t>(contour.signedArea2 < 0 ? -contour.signedArea2 : contour.signedArea2);
				contours.push_back(std::move(contour));
			}

			auto pointInContour = [](const std::pair<int32_t, int32_t>& point, const std::vector<std::pair<int32_t, int32_t>>& polygon) {
				bool inside = false;
				const double x = static_cast<double>(point.first);
				const double y = static_cast<double>(point.second);
				for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
					const double xi = static_cast<double>(polygon[i].first);
					const double yi = static_cast<double>(polygon[i].second);
					const double xj = static_cast<double>(polygon[j].first);
					const double yj = static_cast<double>(polygon[j].second);
					if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) {
						inside = !inside;
					}
				}
				return inside;
				};

			std::vector<size_t> pathGroups;
			for (const EncodedContour& contour : contours) {
				if (contour.pathGroup != 0 && std::find(pathGroups.begin(), pathGroups.end(), contour.pathGroup) == pathGroups.end()) {
					pathGroups.push_back(contour.pathGroup);
				}
			}

			size_t fill0Contours = 0;
			size_t fill1Contours = 0;
			size_t groupedHoleContours = 0;
			for (const EncodedContour& contour : contours) {
				size_t containmentDepth = 0;
				if (contour.pathGroup != 0) {
					double cx = 0.0, cy = 0.0;
					for (const auto& p : contour.points) {
						cx += static_cast<double>(p.first);
						cy += static_cast<double>(p.second);
					}
					cx /= static_cast<double>(contour.points.size());
					cy /= static_cast<double>(contour.points.size());
					const std::pair<int32_t, int32_t> centroid{
						static_cast<int32_t>(cx),
						static_cast<int32_t>(cy),
					};

					for (const EncodedContour& other : contours) {
						if (&other == &contour || other.pathGroup != contour.pathGroup || other.absArea2 <= contour.absArea2) {
							continue;
						}
						if (pointInContour(centroid, other.points)) {
							++containmentDepth;
						}
					}
				}
				const bool isHole = contour.pathGroup != 0 && (containmentDepth % 2u) == 1u;
				const bool clockwiseScreen = contour.signedArea2 > 0;
				const bool useFillStyle1 = isHole ? !clockwiseScreen : clockwiseScreen;
				if (useFillStyle1) {
					++fill1Contours;
				}
				else {
					++fill0Contours;
					++groupedHoleContours;
				}

				const auto first = contour.points.front();
				PutStyleChangeMoveFill(bits, first.first, first.second, shapeFillIndices[contour.shapeIndex], fillBits);
				int32_t prevX = first.first;
				int32_t prevY = first.second;
				if (!contour.segments.empty()) {
					for (const EncodedSegment& segment : contour.segments) {
						if (segment.curved) {
							const int32_t controlDx = segment.control.first - prevX;
							const int32_t controlDy = segment.control.second - prevY;
							const int32_t anchorDx = segment.anchor.first - segment.control.first;
							const int32_t anchorDy = segment.anchor.second - segment.control.second;
							PutCurvedEdge(bits, controlDx, controlDy, anchorDx, anchorDy);
						}
						else {
							PutStraightEdge(bits, segment.anchor.first - prevX, segment.anchor.second - prevY);
						}
						prevX = segment.anchor.first;
						prevY = segment.anchor.second;
					}
				}
				else {
					for (size_t p = 1; p < contour.points.size(); ++p) {
						const auto mapped = contour.points[p];
						PutStraightEdge(bits, mapped.first - prevX, mapped.second - prevY);
						prevX = mapped.first;
						prevY = mapped.second;
					}
					if (first.first != prevX || first.second != prevY) {
						PutStraightEdge(bits, first.first - prevX, first.second - prevY);
					}
				}
			}
			PutBits(bits, 0, 1);
			PutBits(bits, 0, 5);
			const std::vector<uint8_t> packed = PackBits(bits);
			out.insert(out.end(), packed.begin(), packed.end());
			Util::Logger::Instance().Get()->debug(
				"[SWFInject] encoded SVG shape source='{}' shape_id={} fills={} merged_fills={} fill_bits={} paths={} fill0_contours={} fill1_contours={} grouped_holes={} contours={} groups={} bounds=[{},{} -> {},{}] viewBox=[{},{} {}x{}] scale={} offset=({}, {}) shape_tag_bytes={}.",
				portrait.sourcePath,
				portrait.shapeId,
				fills.size(),
				mergedFills,
				fillBits,
				portrait.svg.shapes.size(),
				fill0Contours,
				fill1Contours,
				groupedHoleContours,
				contours.size(),
				pathGroups.size(),
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

		bool FrameContainsDepth1Character(const std::vector<uint8_t>& framePayload, uint16_t characterId) {
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
						if (absoluteCharacterOffset + 2 <= framePayload.size() && ReadU16(framePayload.data() + absoluteCharacterOffset) == characterId) {
							return true;
						}
					}
				}
				cursor = tag.next;
			}
			return false;
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

		bool FindFrameDepth1PlaceObject2CharacterId(const uint8_t* payload, uint32_t length, size_t frameIndex, uint16_t* characterIdOut) {
			if (!payload || length < 4 || !characterIdOut) {
				return false;
			}

			const uint8_t* cursor = payload + 4;
			const uint8_t* end = payload + length;
			size_t currentFrame = 0;
			std::vector<uint8_t> currentFramePayload;

			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					return false;
				}
				if (tag.code == 0) {
					break;
				}
				currentFramePayload.insert(currentFramePayload.end(), tag.begin, tag.next);
				cursor = tag.next;
				if (tag.code == 1 || tag.code == 142) {
					if (currentFrame == frameIndex) {
						const uint8_t* frameBase = currentFramePayload.data();
						const uint8_t* frameCursor = frameBase;
						const uint8_t* frameEnd = frameBase + currentFramePayload.size();
						while (frameCursor < frameEnd) {
							Tag frameTag;
							if (!ReadTag(frameCursor, frameEnd, frameTag)) {
								return false;
							}
							if (frameTag.code == 26 && frameTag.length >= 5) {
								std::vector<uint8_t> place(frameTag.payload, frameTag.next);
								uint16_t depth = 0;
								size_t characterOffset = 0;
								if (PlaceObject2DepthAndCharacterOffset(place, &depth, &characterOffset) && depth == 1) {
									const size_t absoluteCharacterOffset = static_cast<size_t>(frameTag.payload - frameBase) + characterOffset;
									if (absoluteCharacterOffset + 2 <= currentFramePayload.size()) {
										*characterIdOut = ReadU16(currentFramePayload.data() + absoluteCharacterOffset);
										return true;
									}
								}
							}
							frameCursor = frameTag.next;
						}
						return false;
					}
					++currentFrame;
					currentFramePayload.clear();
				}
			}
			return false;
		}

		std::vector<uint8_t> BuildClonedPortraitFrame(const std::vector<uint8_t>& framePayload, const PortraitPlacement& placement, uint16_t spriteId, int sourceFrame) {
			std::vector<uint8_t> clonedFrame = framePayload;
			uint16_t oldCharacterId = 0;
			const bool patchedPlace = PatchFirstDepth1PlaceObject2(clonedFrame, placement.shapeId, &oldCharacterId);
			RegisterInjectedPortraitDepth(placement.shapeId, 1);
			if (!patchedPlace) {
				Util::Logger::Instance().Get()->debug(
					"[SWFInject] sprite={} clone_frame={} failed to find PlaceObject2 depth=1; appending fallback PlaceObject2 shape_id={}",
					spriteId,
					sourceFrame,
					placement.shapeId);
				std::vector<uint8_t> fallback;
				const std::vector<uint8_t> place = EncodePlaceObject2(placement.shapeId, 1);
				AppendTag(fallback, 26, place);
				AppendTag(fallback, 1, {});
				clonedFrame = std::move(fallback);
			}
			Util::Logger::Instance().Get()->debug(
				"[SWFInject] sprite={} cloned frame {} -> PlaceObject2 depth=1 old_character_id={} new_shape_id={} cloned_frame_bytes={} patched_place={}",
				spriteId,
				sourceFrame,
				oldCharacterId,
				placement.shapeId,
				clonedFrame.size(),
				patchedPlace);
			return clonedFrame;
		}

		void AppendClonedPortraitFrames(std::vector<uint8_t>& out, const std::vector<PortraitPlacement>& placements, uint16_t spriteId, int sourceFrame, const std::vector<uint8_t>& framePayload) {
			for (const PortraitPlacement& placement : placements) {
				const std::vector<uint8_t> clonedFrame = BuildClonedPortraitFrame(framePayload, placement, spriteId, sourceFrame);
				out.insert(out.end(), clonedFrame.begin(), clonedFrame.end());
			}
		}
		constexpr uint16_t kLobbyPortraitSpriteId = 180;

		bool PatchLobbyPortraitSprite(const uint8_t* payload, uint32_t length, const InjectionSet& injections, std::vector<uint8_t>& out, uint16_t spriteId) {
			if (!payload || length < 4) {
				return false;
			}

			const uint8_t* end = payload + length;
			const uint16_t oldFrameCount = ReadU16(payload + 2);
			std::vector<std::vector<uint8_t>> frames;
			std::vector<uint8_t> trailingTags;
			std::vector<uint8_t> currentFramePayload;

			const uint8_t* cursor = payload + 4;
			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					break;
				}
				if (tag.code == 0) {
					trailingTags.insert(trailingTags.end(), tag.begin, tag.next);
					cursor = tag.next;
					break;
				}
				currentFramePayload.insert(currentFramePayload.end(), tag.begin, tag.next);
				cursor = tag.next;
				if (tag.code == 1 || tag.code == 142) {
					frames.push_back(std::move(currentFramePayload));
					currentFramePayload.clear();
				}
			}
			if (!currentFramePayload.empty()) {
				trailingTags.insert(trailingTags.end(), currentFramePayload.begin(), currentFramePayload.end());
			}
			if (cursor < end) {
				trailingTags.insert(trailingTags.end(), cursor, end);
			}

			bool insertedClassic = injections.classicPlacements.empty();
			bool insertedFresh = injections.freshPlacements.empty();
			const size_t classicSourceIndex = 33; // frame 34, last vanilla classic portrait frame.
			const size_t classicRemoveIndex = 34; // frame 35, vanilla RemoveObject2 depth=1.
			if (!insertedClassic && frames.size() > classicSourceIndex) {
				if (frames.size() > classicRemoveIndex + injections.classicPlacements.size()) {
					const std::vector<uint8_t> removeFrame = frames[classicRemoveIndex];
					for (size_t i = 0; i < injections.classicPlacements.size(); ++i) {
						frames[classicRemoveIndex + i] = BuildClonedPortraitFrame(frames[classicSourceIndex], injections.classicPlacements[i], spriteId, 34);
					}
					frames[classicRemoveIndex + injections.classicPlacements.size()] = removeFrame;
					insertedClassic = true;
					Util::Logger::Instance().Get()->debug(
						"[SWFInject] sprite={} placed {} classic portrait frame(s) in existing empty timeline space starting at frame 35; no fresh frame shift.",
						spriteId,
						injections.classicPlacements.size());
				}
				else {
					Util::Logger::Instance().Get()->warn(
						"[SWFInject] sprite={} cannot place {} classic portrait frame(s) in-place; frame_count={} source_index={}.",
						spriteId,
						injections.classicPlacements.size(),
						frames.size(),
						classicSourceIndex);
				}
			}

			if (!insertedFresh) {
				for (size_t i = 0; i < frames.size(); ++i) {
					if (FrameContainsDepth1Character(frames[i], 153)) {
						std::vector<std::vector<uint8_t>> clonedFreshFrames;
						clonedFreshFrames.reserve(injections.freshPlacements.size());
						for (const PortraitPlacement& placement : injections.freshPlacements) {
							clonedFreshFrames.push_back(BuildClonedPortraitFrame(frames[i], placement, spriteId, static_cast<int>(i + 1)));
						}
						frames.insert(frames.begin() + static_cast<std::ptrdiff_t>(i + 1), clonedFreshFrames.begin(), clonedFreshFrames.end());
						insertedFresh = true;
						break;
					}
				}
			}

			const uint16_t newFrameCount = static_cast<uint16_t>(std::min<size_t>(65535, frames.size()));
			out.clear();
			out.reserve(length + injections.freshPlacements.size() * 512);
			out.insert(out.end(), payload, payload + 4);
			out[2] = static_cast<uint8_t>(newFrameCount & 0xFF);
			out[3] = static_cast<uint8_t>((newFrameCount >> 8) & 0xFF);
			for (const auto& framePayload : frames) {
				out.insert(out.end(), framePayload.begin(), framePayload.end());
			}
			out.insert(out.end(), trailingTags.begin(), trailingTags.end());

			Util::Logger::Instance().Get()->debug(
				"[SWFInject] sprite={} frame_count old={} new={} classic_inplace={} fresh_inserted={} classic_frames={} fresh_frames={}.",
				spriteId,
				oldFrameCount,
				newFrameCount,
				insertedClassic,
				insertedFresh,
				injections.classicPlacements.size(),
				injections.freshPlacements.size());

			if (!insertedClassic || !insertedFresh) {
				Util::Logger::Instance().Get()->warn(
					"[SWFInject] lobby portrait sprite={} incomplete insertion classic_done={} fresh_done={} classic_count={} fresh_count={}",
					spriteId,
					insertedClassic,
					insertedFresh,
					injections.classicPlacements.size(),
					injections.freshPlacements.size());
			}
			return insertedClassic && insertedFresh;
		}		bool PatchMenuPortraitSprite(const uint8_t* payload, uint32_t length, const InjectionSet& injections, std::vector<uint8_t>& out, uint16_t spriteId) {
			if (!payload || length < 4) {
				return false;
			}

			const uint8_t* end = payload + length;
			const uint16_t oldFrameCount = ReadU16(payload + 2);
			std::vector<std::vector<uint8_t>> frames;
			std::vector<uint8_t> trailingTags;
			std::vector<uint8_t> currentFramePayload;

			const uint8_t* cursor = payload + 4;
			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					Util::Logger::Instance().Get()->warn(
						"[SWFInject] menu sprite={} frame parse BROKE at offset={} frames_so_far={} remaining={}.",
						spriteId,
						static_cast<size_t>(cursor - payload),
						frames.size(),
						static_cast<size_t>(end - cursor));
					break;
				}
				if (tag.code == 0) {
					Util::Logger::Instance().Get()->debug(
						"[SWFInject] menu sprite={} hit End tag at offset={} frames_parsed={} sprite_payload_len={}.",
						spriteId,
						static_cast<size_t>(cursor - payload),
						frames.size(),
						length);
					trailingTags.insert(trailingTags.end(), tag.begin, tag.next);
					cursor = tag.next;
					break;
				}
				currentFramePayload.insert(currentFramePayload.end(), tag.begin, tag.next);
				cursor = tag.next;
				if (tag.code == 1 || tag.code == 142) {
					frames.push_back(std::move(currentFramePayload));
					currentFramePayload.clear();
				}
			}
			if (!currentFramePayload.empty()) {
				trailingTags.insert(trailingTags.end(), currentFramePayload.begin(), currentFramePayload.end());
			}
			if (cursor < end) {
				trailingTags.insert(trailingTags.end(), cursor, end);
			}

			bool insertedClassic = injections.classicPlacements.empty();
			bool insertedFresh = injections.freshPlacements.empty();

			const size_t classicSourceIndex = 30; // frame 31, last vanilla classic menu portrait template.
			const size_t classicInsertIndex = 31; // frame 32, first addon classic portrait slot; frame 32 remove moves to frame 33.
			if (!insertedClassic && frames.size() > classicSourceIndex && frames.size() > classicInsertIndex + injections.classicPlacements.size()) {
				const std::vector<uint8_t> oldFrame32 = frames[classicInsertIndex];
				for (size_t i = 0; i < injections.classicPlacements.size(); ++i) {
					frames[classicInsertIndex + i] = BuildClonedPortraitFrame(
						frames[classicSourceIndex],
						injections.classicPlacements[i],
						spriteId,
						static_cast<int>(classicSourceIndex + 1));
				}
				frames[classicInsertIndex + injections.classicPlacements.size()] = oldFrame32;
				insertedClassic = true;
				Util::Logger::Instance().Get()->debug(
					"[SWFInject] menu sprite={} placed {} classic portrait frame(s) at frame 32 and moved old frame 32 after them.",
					spriteId,
					injections.classicPlacements.size());
			}
			else if (!insertedClassic) {
				Util::Logger::Instance().Get()->debug(
					"[SWFInject] menu sprite={} cannot place classic portrait(s): frames={} source={} insert={} count={}.",
					spriteId,
					frames.size(),
					classicSourceIndex,
					classicInsertIndex,
					injections.classicPlacements.size());
			}

			const size_t freshSourceIndex = 131; // frame 132, last vanilla fresh menu portrait template.
			if (!insertedFresh && frames.size() > freshSourceIndex) {
				std::vector<std::vector<uint8_t>> clonedFreshFrames;
				clonedFreshFrames.reserve(injections.freshPlacements.size());
				for (const PortraitPlacement& placement : injections.freshPlacements) {
					clonedFreshFrames.push_back(BuildClonedPortraitFrame(
						frames[freshSourceIndex],
						placement,
						spriteId,
						static_cast<int>(freshSourceIndex + 1)));
				}
				frames.insert(frames.begin() + static_cast<std::ptrdiff_t>(freshSourceIndex + 1), clonedFreshFrames.begin(), clonedFreshFrames.end());
				insertedFresh = true;
				Util::Logger::Instance().Get()->debug(
					"[SWFInject] menu sprite={} inserted {} fresh portrait frame(s) after frame 132.",
					spriteId,
					injections.freshPlacements.size());
			}
			else if (!insertedFresh) {
				Util::Logger::Instance().Get()->debug(
					"[SWFInject] menu sprite={} cannot place fresh portrait(s): frames={} source={} count={}.",
					spriteId,
					frames.size(),
					freshSourceIndex,
					injections.freshPlacements.size());
			}

			const uint16_t newFrameCount = static_cast<uint16_t>(std::min<size_t>(65535, frames.size()));
			out.clear();
			out.reserve(length + (injections.classicPlacements.size() + injections.freshPlacements.size()) * 512);
			out.insert(out.end(), payload, payload + 4);
			out[2] = static_cast<uint8_t>(newFrameCount & 0xFF);
			out[3] = static_cast<uint8_t>((newFrameCount >> 8) & 0xFF);
			for (const auto& framePayload : frames) {
				out.insert(out.end(), framePayload.begin(), framePayload.end());
			}
			out.insert(out.end(), trailingTags.begin(), trailingTags.end());

			Util::Logger::Instance().Get()->debug(
				"[SWFInject] menu sprite={} frame_count old={} new={} classic_done={} fresh_done={} classic_frames={} fresh_frames={}",
				spriteId,
				oldFrameCount,
				newFrameCount,
				insertedClassic,
				insertedFresh,
				injections.classicPlacements.size(),
				injections.freshPlacements.size());

			return insertedClassic && insertedFresh;
		}
		bool PatchMapPortraitSprite(const uint8_t* payload, uint32_t length, const InjectionSet& injections, std::vector<uint8_t>& out, uint16_t spriteId) {
			if (!payload || length < 4) {
				return false;
			}

			const uint8_t* end = payload + length;
			const uint16_t oldFrameCount = ReadU16(payload + 2);
			std::vector<std::vector<uint8_t>> frames;
			std::vector<uint8_t> trailingTags;
			std::vector<uint8_t> currentFramePayload;

			const uint8_t* cursor = payload + 4;
			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					return false;
				}
				if (tag.code == 0) {
					if (!currentFramePayload.empty()) {
						trailingTags.insert(trailingTags.end(), currentFramePayload.begin(), currentFramePayload.end());
						currentFramePayload.clear();
					}
					trailingTags.insert(trailingTags.end(), tag.begin, tag.next);
					break;
				}
				currentFramePayload.insert(currentFramePayload.end(), tag.begin, tag.next);
				if (tag.code == 1) {
					frames.push_back(std::move(currentFramePayload));
					currentFramePayload.clear();
				}
				cursor = tag.next;
			}
			if (!currentFramePayload.empty()) {
				trailingTags.insert(trailingTags.end(), currentFramePayload.begin(), currentFramePayload.end());
			}
			if (frames.empty()) {
				Util::Logger::Instance().Get()->warn("[SWFInject] map portrait sprite={} has no parsed frames old_frame_count={} length={}", spriteId, oldFrameCount, length);
				return false;
			}

			bool insertedClassic = injections.classicPlacements.empty();
			const size_t classicSourceIndex = 33; // frame 34, last vanilla classic map portrait template.
			const size_t removeIndex = 34; // frame 35, vanilla RemoveObject.
			if (!insertedClassic && frames.size() > classicSourceIndex && frames.size() > removeIndex + injections.classicPlacements.size()) {
				const std::vector<uint8_t> oldRemoveFrame = frames[removeIndex];
				for (size_t i = 0; i < injections.classicPlacements.size(); ++i) {
					frames[removeIndex + i] = BuildClonedPortraitFrame(frames[classicSourceIndex], injections.classicPlacements[i], spriteId, static_cast<int>(classicSourceIndex + 1));
				}
				frames[removeIndex + injections.classicPlacements.size()] = oldRemoveFrame;
				insertedClassic = true;
				Util::Logger::Instance().Get()->debug("[SWFInject] map sprite={} placed {} classic map portrait frame(s) at frame 35 and moved remove after them.", spriteId, injections.classicPlacements.size());
			}
			else if (!insertedClassic) {
				Util::Logger::Instance().Get()->warn("[SWFInject] map sprite={} cannot place classic map portrait(s): frames={} source={} remove={} count={}", spriteId, frames.size(), classicSourceIndex, removeIndex, injections.classicPlacements.size());
			}

			const uint16_t newFrameCount = static_cast<uint16_t>(std::min<size_t>(65535, frames.size()));
			out.clear();
			out.reserve(length + injections.classicPlacements.size() * 512);
			out.insert(out.end(), payload, payload + 4);
			out[2] = static_cast<uint8_t>(newFrameCount & 0xFF);
			out[3] = static_cast<uint8_t>((newFrameCount >> 8) & 0xFF);
			for (const auto& framePayload : frames) {
				out.insert(out.end(), framePayload.begin(), framePayload.end());
			}
			out.insert(out.end(), trailingTags.begin(), trailingTags.end());
			Util::Logger::Instance().Get()->debug(
				"[SWFInject] map sprite={} frame_count {}->{} classic_done={} classic_count={}",
				spriteId,
				oldFrameCount,
				newFrameCount,
				insertedClassic,
				injections.classicPlacements.size());
			return insertedClassic;
		}
		bool PatchMapNewFreshPortraitSprite(const uint8_t* payload, uint32_t length, const InjectionSet& injections, std::vector<uint8_t>& out, uint16_t spriteId) {
			if (!payload || length < 4) {
				return false;
			}

			const uint8_t* end = payload + length;
			const uint16_t oldFrameCount = ReadU16(payload + 2);
			std::vector<std::vector<uint8_t>> frames;
			std::vector<uint8_t> trailingTags;
			std::vector<uint8_t> currentFramePayload;

			const uint8_t* cursor = payload + 4;
			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					return false;
				}
				if (tag.code == 0) {
					if (!currentFramePayload.empty()) {
						trailingTags.insert(trailingTags.end(), currentFramePayload.begin(), currentFramePayload.end());
						currentFramePayload.clear();
					}
					trailingTags.insert(trailingTags.end(), tag.begin, tag.next);
					break;
				}
				currentFramePayload.insert(currentFramePayload.end(), tag.begin, tag.next);
				if (tag.code == 1 || tag.code == 142) {
					frames.push_back(std::move(currentFramePayload));
					currentFramePayload.clear();
				}
				cursor = tag.next;
			}
			if (!currentFramePayload.empty()) {
				trailingTags.insert(trailingTags.end(), currentFramePayload.begin(), currentFramePayload.end());
			}
			if (frames.empty()) {
				Util::Logger::Instance().Get()->warn("[SWFInject] map_new sprite={} has no parsed frames old_frame_count={} length={}", spriteId, oldFrameCount, length);
				return false;
			}

			constexpr size_t kFreshSourceIndex = 134; // frame 135, vanilla fresh portrait template.
			if (!injections.freshPlacements.empty()) {
				if (frames.size() <= kFreshSourceIndex) {
					Util::Logger::Instance().Get()->warn(
						"[SWFInject] map_new sprite={} cannot place fresh portrait(s): frames={} source={} count={}",
						spriteId,
						frames.size(),
						kFreshSourceIndex,
						injections.freshPlacements.size());
					return false;
				}

				std::vector<std::vector<uint8_t>> clonedFreshFrames;
				clonedFreshFrames.reserve(injections.freshPlacements.size());
				for (const PortraitPlacement& placement : injections.freshPlacements) {
					clonedFreshFrames.push_back(BuildClonedPortraitFrame(frames[kFreshSourceIndex], placement, spriteId, static_cast<int>(kFreshSourceIndex + 1)));
				}
				frames.insert(frames.begin() + static_cast<std::ptrdiff_t>(kFreshSourceIndex + 1), clonedFreshFrames.begin(), clonedFreshFrames.end());
			}

			const uint16_t newFrameCount = static_cast<uint16_t>(std::min<size_t>(65535, frames.size()));
			out.clear();
			out.reserve(length + injections.freshPlacements.size() * 512);
			out.insert(out.end(), payload, payload + 4);
			out[2] = static_cast<uint8_t>(newFrameCount & 0xFF);
			out[3] = static_cast<uint8_t>((newFrameCount >> 8) & 0xFF);
			for (const auto& framePayload : frames) {
				out.insert(out.end(), framePayload.begin(), framePayload.end());
			}
			out.insert(out.end(), trailingTags.begin(), trailingTags.end());

			Util::Logger::Instance().Get()->info(
				"[SWFInject] map_new sprite={} frame_count {}->{} inserted_fresh={} source_frame=135",
				spriteId,
				oldFrameCount,
				newFrameCount,
				injections.freshPlacements.size());
			return true;
		}

		bool PatchMainPortraitIconSprite(const uint8_t* payload, uint32_t length, const InjectionSet& injections, std::vector<uint8_t>& out, uint16_t spriteId) {
			if (!payload || length < 4) {
				return false;
			}

			const uint8_t* end = payload + length;
			const uint16_t oldFrameCount = ReadU16(payload + 2);
			std::vector<std::vector<uint8_t>> frames;
			std::vector<uint8_t> trailingTags;
			std::vector<uint8_t> currentFramePayload;

			const uint8_t* cursor = payload + 4;
			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					return false;
				}
				if (tag.code == 0) {
					if (!currentFramePayload.empty()) {
						trailingTags.insert(trailingTags.end(), currentFramePayload.begin(), currentFramePayload.end());
						currentFramePayload.clear();
					}
					trailingTags.insert(trailingTags.end(), tag.begin, tag.next);
					break;
				}
				currentFramePayload.insert(currentFramePayload.end(), tag.begin, tag.next);
				if (tag.code == 1) {
					frames.push_back(std::move(currentFramePayload));
					currentFramePayload.clear();
				}
				cursor = tag.next;
			}
			if (!currentFramePayload.empty()) {
				trailingTags.insert(trailingTags.end(), currentFramePayload.begin(), currentFramePayload.end());
			}
			if (frames.empty()) {
				Util::Logger::Instance().Get()->warn("[SWFInject] main icon sprite={} has no parsed frames old_frame_count={} length={}", spriteId, oldFrameCount, length);
				return false;
			}

			bool insertedClassic = injections.classicPlacements.empty();
			bool insertedFresh = injections.freshPlacements.empty();

			const size_t classicSourceIndex = 31; // frame 32, last vanilla classic icon template.
			const size_t classicInsertIndex = 32; // frame 33 RemoveObject becomes after addon classic icons.
			if (!insertedClassic && frames.size() > classicSourceIndex && frames.size() > classicInsertIndex + injections.classicPlacements.size()) {
				const std::vector<uint8_t> oldFrame33 = frames[classicInsertIndex];
				for (size_t i = 0; i < injections.classicPlacements.size(); ++i) {
					frames[classicInsertIndex + i] = BuildClonedPortraitFrame(frames[classicSourceIndex], injections.classicPlacements[i], spriteId, static_cast<int>(classicSourceIndex + 1));
				}
				frames[classicInsertIndex + injections.classicPlacements.size()] = oldFrame33;
				insertedClassic = true;
				Util::Logger::Instance().Get()->debug("[SWFInject] main icon sprite={} placed {} classic icon frame(s) after frame 32.", spriteId, injections.classicPlacements.size());
			}
			else if (!insertedClassic) {
				Util::Logger::Instance().Get()->warn("[SWFInject] main icon sprite={} cannot place classic icon(s): frames={} source={} insert={} count={}", spriteId, frames.size(), classicSourceIndex, classicInsertIndex, injections.classicPlacements.size());
			}

			const size_t freshSourceIndex = 132; // frame 133, last vanilla fresh icon template.
			if (!insertedFresh && frames.size() > freshSourceIndex) {
				std::vector<std::vector<uint8_t>> clonedFreshFrames;
				clonedFreshFrames.reserve(injections.freshPlacements.size());
				for (const PortraitPlacement& placement : injections.freshPlacements) {
					clonedFreshFrames.push_back(BuildClonedPortraitFrame(frames[freshSourceIndex], placement, spriteId, static_cast<int>(freshSourceIndex + 1)));
				}
				frames.insert(frames.begin() + static_cast<std::ptrdiff_t>(freshSourceIndex + 1), clonedFreshFrames.begin(), clonedFreshFrames.end());
				insertedFresh = true;
				Util::Logger::Instance().Get()->debug("[SWFInject] main icon sprite={} inserted {} fresh icon frame(s) after frame 133.", spriteId, injections.freshPlacements.size());
			}
			else if (!insertedFresh) {
				Util::Logger::Instance().Get()->warn("[SWFInject] main icon sprite={} cannot place fresh icon(s): frames={} source={} count={}", spriteId, frames.size(), freshSourceIndex, injections.freshPlacements.size());
			}

			const uint16_t newFrameCount = static_cast<uint16_t>(std::min<size_t>(65535, frames.size()));
			out.clear();
			out.reserve(length + (injections.classicPlacements.size() + injections.freshPlacements.size()) * 512);
			out.insert(out.end(), payload, payload + 4);
			out[2] = static_cast<uint8_t>(newFrameCount & 0xFF);
			out[3] = static_cast<uint8_t>((newFrameCount >> 8) & 0xFF);
			for (const auto& framePayload : frames) {
				out.insert(out.end(), framePayload.begin(), framePayload.end());
			}
			out.insert(out.end(), trailingTags.begin(), trailingTags.end());
			Util::Logger::Instance().Get()->debug(
				"[SWFInject] main icon sprite={} frame_count {}->{} classic_done={} fresh_done={} classic_count={} fresh_count={}",
				spriteId,
				oldFrameCount,
				newFrameCount,
				insertedClassic,
				insertedFresh,
				injections.classicPlacements.size(),
				injections.freshPlacements.size());
			return insertedClassic && insertedFresh;
		}
		void AppendDefinitions(std::vector<uint8_t>& out, const InjectionSet& injections) {
			for (const PortraitDefinition& portrait : injections.definitions) {
				const std::vector<uint8_t> metadata = EncodeMetadata(portrait);
				const std::vector<uint8_t> bounds = injections.shapeTemplate152BoundsBytes.empty()
					? EncodeRect(0, static_cast<int32_t>(portrait.svg.width) * 20, 0, static_cast<int32_t>(portrait.svg.height) * 20)
					: injections.shapeTemplate152BoundsBytes;
				if (portrait.isImportedSwf) {
					std::vector<uint8_t> importedShape;
					if (!ScaleDefineShapePayloadToBounds(portrait.importedShapePayload, portrait.importedShapeTagCode, injections.shapeTemplate152Bounds, bounds, importedShape)) {
						Util::Logger::Instance().Get()->warn(
							"[SWFInject] failed to geometrically scale imported SWF portrait '{}'; writing original shape payload.",
							portrait.sourcePath);
						importedShape = portrait.importedShapePayload;
					}
					AppendTagLong(out, portrait.importedShapeTagCode, importedShape);
					LogShapeDiagnostics("imported_swf", importedShape, portrait.importedShapeTagCode);
					continue;
				}
				AppendTag(out, 77, metadata);
				if (portrait.isSvg) {
					const std::vector<uint8_t> shape = EncodeSvgShape(portrait, injections.shapeTemplate152Bounds, bounds);
					LogShapeDiagnostics("injected_svg", shape, 2);
					AppendTagLong(out, 2, shape);
				}
				else {
					const std::vector<uint8_t> bitmap = EncodeDefineBitsLossless2(portrait);
					AppendTagLong(out, 20, bitmap);
					const std::vector<uint8_t> shape = EncodeBitmapFillShape(portrait, injections.shapeTemplate152Bounds, bounds);
					AppendTagLong(out, 2, shape);
				}
			}
		}

		size_t PatchPlaceObjectCharacterReferences(std::vector<uint8_t>& swf, size_t beginOffset, size_t endOffset, uint16_t oldCharacterId, uint16_t newCharacterId) {
			if (beginOffset >= endOffset || endOffset > swf.size()) {
				return 0;
			}

			size_t patchedCount = 0;
			const uint8_t* base = swf.data();
			const uint8_t* cursor = base + beginOffset;
			const uint8_t* end = base + endOffset;
			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					break;
				}

				if (tag.code == 4 && tag.length >= 4) {
					const size_t characterOffset = static_cast<size_t>(tag.payload - base);
					if (ReadU16(tag.payload) == oldCharacterId) {
						PatchU16(swf, characterOffset, newCharacterId);
						++patchedCount;
					}
				}
				else if (tag.code == 26 && tag.length >= 5) {
					const uint8_t flags = tag.payload[0];
					if ((flags & 0x02) != 0) {
						const size_t characterOffset = static_cast<size_t>(tag.payload - base) + 3;
						if (characterOffset + 2 <= swf.size() && ReadU16(base + characterOffset) == oldCharacterId) {
							PatchU16(swf, characterOffset, newCharacterId);
							++patchedCount;
						}
					}
				}
				else if (tag.code == 39 && tag.length >= 4) {
					patchedCount += PatchPlaceObjectCharacterReferences(
						swf,
						static_cast<size_t>((tag.payload + 4) - base),
						static_cast<size_t>(tag.next - base),
						oldCharacterId,
						newCharacterId);
				}

				cursor = tag.next;
				if (tag.code == 0) {
					break;
				}
			}
			return patchedCount;
		}
		bool FindCharacterTagByIdRaw(const uint8_t* cursor, const uint8_t* end, uint16_t code, uint16_t characterId, Tag& tag) {
			if (!cursor || !end || cursor >= end) {
				return false;
			}

			for (const uint8_t* scan = cursor; scan + 6 <= end; ++scan) {
				const uint16_t packed = ReadU16(scan);
				if ((packed >> 6) != code) {
					continue;
				}

				const uint8_t* payload = scan + 2;
				uint32_t length = packed & 0x3F;
				if (length == 0x3F) {
					if (scan + 6 > end) {
						continue;
					}
					length = ReadU32(scan + 2);
					payload = scan + 6;
				}

				if (length < 2 || payload + length > end) {
					continue;
				}
				if (ReadU16(payload) != characterId) {
					continue;
				}

				tag.code = code;
				tag.length = length;
				tag.begin = scan;
				tag.payload = payload;
				tag.next = payload + length;
				return true;
			}
			return false;
		}

		bool DecodeCxformByteLength(const uint8_t* data, size_t length, size_t& byteLength) {
			if (!data || length == 0) {
				return false;
			}
			size_t bitOffset = 0;
			if (bitOffset + 6 > length * 8u) {
				return false;
			}
			const bool hasAddTerms = ReadPackedBits(data, bitOffset, 1) != 0;
			bitOffset += 1;
			const bool hasMultTerms = ReadPackedBits(data, bitOffset, 1) != 0;
			bitOffset += 1;
			const uint32_t nbits = ReadPackedBits(data, bitOffset, 4);
			bitOffset += 4;
			const uint32_t termCount = (hasMultTerms ? 4u : 0u) + (hasAddTerms ? 4u : 0u);
			bitOffset += termCount * nbits;
			byteLength = (bitOffset + 7u) / 8u;
			return byteLength <= length;
		}

		bool PatchPlaceObject2NamePayload(std::vector<uint8_t>& payload, uint16_t characterId, const char* newName) {
			if (payload.size() < 3 || !newName) {
				return false;
			}
			const uint8_t flags = payload[0];
			if ((flags & 0x02) == 0) {
				return false;
			}
			size_t cursor = 3;
			if (cursor + 2 > payload.size() || ReadU16(payload.data() + cursor) != characterId) {
				return false;
			}
			cursor += 2;
			if ((flags & 0x04) != 0) {
				size_t matrixBytes = 0;
				if (!DecodeMatrixByteLength(payload.data() + cursor, payload.size() - cursor, matrixBytes)) {
					return false;
				}
				cursor += matrixBytes;
			}
			if ((flags & 0x08) != 0) {
				size_t cxformBytes = 0;
				if (!DecodeCxformByteLength(payload.data() + cursor, payload.size() - cursor, cxformBytes)) {
					return false;
				}
				cursor += cxformBytes;
			}
			if ((flags & 0x10) != 0) {
				if (cursor + 2 > payload.size()) {
					return false;
				}
				cursor += 2;
			}
			if ((flags & 0x20) == 0 || cursor >= payload.size()) {
				return false;
			}
			size_t end = cursor;
			while (end < payload.size() && payload[end] != 0) {
				++end;
			}
			if (end >= payload.size()) {
				return false;
			}
			const size_t newLen = std::strlen(newName);
			std::vector<uint8_t> rebuilt;
			rebuilt.reserve(payload.size() - (end - cursor) + newLen);
			rebuilt.insert(rebuilt.end(), payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(cursor));
			rebuilt.insert(rebuilt.end(), reinterpret_cast<const uint8_t*>(newName), reinterpret_cast<const uint8_t*>(newName) + newLen);
			rebuilt.push_back(0);
			rebuilt.insert(rebuilt.end(), payload.begin() + static_cast<std::ptrdiff_t>(end + 1), payload.end());
			payload.swap(rebuilt);
			return true;
		}

		size_t PatchPlaceObject2NamesForCharacter(std::vector<uint8_t>& swf, size_t beginOffset, size_t endOffset, uint16_t characterId, const char* newName) {
			if (beginOffset >= endOffset || endOffset > swf.size()) {
				return 0;
			}
			size_t patchedCount = 0;
			size_t offset = beginOffset;
			while (offset < endOffset && offset < swf.size()) {
				Tag tag;
				const uint8_t* base = swf.data();
				if (!ReadTag(base + offset, base + endOffset, tag)) {
					break;
				}
				const size_t tagBytes = static_cast<size_t>(tag.next - tag.begin);
				if (tag.code == 26) {
					std::vector<uint8_t> payload(tag.payload, tag.next);
					if (PatchPlaceObject2NamePayload(payload, characterId, newName)) {
						std::vector<uint8_t> encoded;
						AppendTag(encoded, 26, payload);
						swf.erase(swf.begin() + static_cast<std::ptrdiff_t>(offset), swf.begin() + static_cast<std::ptrdiff_t>(offset + tagBytes));
						swf.insert(swf.begin() + static_cast<std::ptrdiff_t>(offset), encoded.begin(), encoded.end());
						const ptrdiff_t delta = static_cast<ptrdiff_t>(encoded.size()) - static_cast<ptrdiff_t>(tagBytes);
						endOffset = static_cast<size_t>(static_cast<ptrdiff_t>(endOffset) + delta);
						offset += encoded.size();
						++patchedCount;
						continue;
					}
				}
				offset += tagBytes;
				if (tag.code == 0) {
					break;
				}
			}
			return patchedCount;
		}
		bool ReplaceAsciiCString(std::vector<uint8_t>& payload, const char* oldText, const char* newText) {
			if (!oldText || !newText || !*oldText) {
				return false;
			}
			const size_t oldLen = std::strlen(oldText);
			const size_t newLen = std::strlen(newText);
			for (size_t i = 0; i + oldLen < payload.size(); ++i) {
				if (std::memcmp(payload.data() + i, oldText, oldLen) != 0) {
					continue;
				}
				if (payload[i + oldLen] != 0) {
					continue;
				}
				std::vector<uint8_t> rebuilt;
				rebuilt.reserve(payload.size() - oldLen + newLen);
				rebuilt.insert(rebuilt.end(), payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(i));
				rebuilt.insert(rebuilt.end(), reinterpret_cast<const uint8_t*>(newText), reinterpret_cast<const uint8_t*>(newText) + newLen);
				rebuilt.push_back(0);
				rebuilt.insert(rebuilt.end(), payload.begin() + static_cast<std::ptrdiff_t>(i + oldLen + 1), payload.end());
				payload.swap(rebuilt);
				return true;
			}
			return false;
		}

		std::vector<uint8_t> CloneCharacterPayload(const Tag& tag, uint16_t newCharacterId) {
			std::vector<uint8_t> payload(tag.payload, tag.payload + tag.length);
			PatchU16(payload, 0, newCharacterId);
			return payload;
		}

		std::vector<uint8_t> BuildExportAssetsPayload(uint16_t characterId, const char* name) {
			std::vector<uint8_t> payload;
			WriteU16(payload, 1);
			WriteU16(payload, characterId);
			payload.insert(payload.end(), name, name + std::strlen(name));
			payload.push_back(0);
			return payload;
		}

		uint16_t FindNextAvailableCharacterId(const SWFBufferView& view, const uint8_t* physicalEnd, bool rawCharacterIdScan) {
			std::array<bool, 65536> usedCharacterIds{};
			const uint8_t* idScanEnd = physicalEnd ? physicalEnd : view.base + view.fileLength;
			MarkKnownCharacterIds(view.tags, idScanEnd, usedCharacterIds);
			if (rawCharacterIdScan) {
				MarkKnownCharacterIdsPhysicalSequence(view.tags, idScanEnd, usedCharacterIds);
			}
			uint16_t maxCharacterId = 0;
			for (uint32_t id = 1; id < usedCharacterIds.size(); ++id) {
				if (usedCharacterIds[id]) {
					maxCharacterId = static_cast<uint16_t>(id);
				}
			}
			return static_cast<uint16_t>(maxCharacterId + 1);
		}

		uint16_t FindRemappedCharacterId(const std::vector<std::pair<uint16_t, uint16_t>>& remap, uint16_t oldId);
		bool ShouldCopyImportedSwfTag(uint16_t code);
		void PatchDefineEditTextFontReference(std::vector<uint8_t>& payload, const std::vector<std::pair<uint16_t, uint16_t>>& remap);
		void InsertStopActionOnFirstSpriteFrame(std::vector<uint8_t>& spritePayload, bool fetchLobbies);

		MenuOnlineCompatibilityInjection BuildMenuOnlineCompatibilityInjection(const SWFBufferView& view, const uint8_t* physicalEnd) {
			MenuOnlineCompatibilityInjection injection;
			SWFBufferView imported = MakeSWFBufferView(const_cast<uint8_t*>(kOnlineCompatibilitySwfData));
			if (!imported.valid || imported.compressed || imported.fileLength != kOnlineCompatibilitySwfSize || !imported.tags) {
				Util::Logger::Instance().Get()->warn(
					"[SWFInject] skip onlinecompatibility import: invalid embedded SWF valid={} compressed={} length={} expected={}",
					imported.valid,
					imported.compressed,
					imported.fileLength,
					kOnlineCompatibilitySwfSize);
				return injection;
			}

			uint16_t nextId = FindNextAvailableCharacterId(view, physicalEnd, true);
			std::array<bool, 65536> importedCharacterIds{};
			const uint8_t* cursor = imported.tags;
			const uint8_t* end = imported.base + imported.fileLength;
			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					break;
				}
				if (IsCharacterIdTag(tag.code) && tag.length >= 2) {
					importedCharacterIds[ReadU16(tag.payload)] = true;
				}
				cursor = tag.next;
				if (tag.code == 0) {
					break;
				}
			}

			std::vector<std::pair<uint16_t, uint16_t>> remap;
			for (uint32_t oldId = 1; oldId < importedCharacterIds.size(); ++oldId) {
				if (!importedCharacterIds[oldId]) {
					continue;
				}
				if (nextId == 0 || nextId >= 65535) {
					Util::Logger::Instance().Get()->warn("[SWFInject] skip onlinecompatibility import: no ids left next_id={}", nextId);
					return injection;
				}
				remap.emplace_back(static_cast<uint16_t>(oldId), nextId++);
			}

			cursor = imported.tags;
			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					break;
				}
				if (ShouldCopyImportedSwfTag(tag.code) && tag.length >= 2) {
					std::vector<uint8_t> payload(tag.payload, tag.payload + tag.length);
					const uint16_t oldId = ReadU16(payload.data());
					if (oldId == 24 || oldId == 25 || oldId == 26) {
						cursor = tag.next;
						continue;
					}
					const uint16_t newId = FindRemappedCharacterId(remap, oldId);
					PatchU16(payload, 0, newId);

					if (tag.code == 39) {
						for (const auto& pair : remap) {
							PatchPlaceObjectCharacterReferences(payload, 4, payload.size(), pair.first, pair.second);
						}
						if (oldId == 292) {
							PatchPlaceObjectCharacterReferences(payload, 4, payload.size(), FindRemappedCharacterId(remap, 26), 855);
						}
						if (oldId == 291) {
							PatchPlaceObject2NamesForCharacter(payload, 4, payload.size(), FindRemappedCharacterId(remap, 289), "textPlayVanilla");
							PatchPlaceObject2NamesForCharacter(payload, 4, payload.size(), FindRemappedCharacterId(remap, 290), "textPlayModded");
						}
						if (oldId == 292) {
							InsertStopActionOnFirstSpriteFrame(payload, false);
						}
					}
					else if (tag.code == 37) {
						PatchDefineEditTextFontReference(payload, remap);
					}

					injection.definitions.push_back({ tag.code, std::move(payload) });
				}
				cursor = tag.next;
				if (tag.code == 0) {
					break;
				}
			}

			const uint16_t rootSpriteId = FindRemappedCharacterId(remap, 292);
			if (rootSpriteId == 292) {
				Util::Logger::Instance().Get()->warn("[SWFInject] skip onlinecompatibility import: root sprite 292 was not remapped.");
				return injection;
			}
			injection.definitions.push_back({ 56, BuildExportAssetsPayload(rootSpriteId, "onlinecompatibility") });
			injection.exportedSpriteId = rootSpriteId;
			injection.nextCharacterId = nextId;
			injection.valid = true;
			Util::Logger::Instance().Get()->info(
				"[SWFInject] prepared onlinecompatibility import root_old=292 root_new={} definitions={} remap_count={} next_id={}",
				rootSpriteId,
				injection.definitions.size(),
				remap.size(),
				nextId);
			return injection;
		}
		uint16_t FindRemappedCharacterId(const std::vector<std::pair<uint16_t, uint16_t>>& remap, uint16_t oldId) {
			for (const auto& pair : remap) {
				if (pair.first == oldId) {
					return pair.second;
				}
			}
			return oldId;
		}

		void PatchKnownCharacterWords(std::vector<uint8_t>& payload, size_t beginOffset, const std::vector<std::pair<uint16_t, uint16_t>>& remap) {
			if (beginOffset + 1 >= payload.size()) {
				return;
			}
			for (size_t offset = beginOffset; offset + 1 < payload.size(); ++offset) {
				const uint16_t value = ReadU16(payload.data() + offset);
				const uint16_t mapped = FindRemappedCharacterId(remap, value);
				if (mapped != value) {
					PatchU16(payload, offset, mapped);
				}
			}
		}
		void PatchDefineEditTextFontReference(std::vector<uint8_t>& payload, const std::vector<std::pair<uint16_t, uint16_t>>& remap) {
			if (payload.size() < 6) {
				return;
			}
			const size_t boundsBytes = RectByteLength(payload.data() + 2, payload.size() - 2);
			const size_t flagsOffset = 2 + boundsBytes;
			if (!boundsBytes || flagsOffset + 4 > payload.size()) {
				return;
			}
			const uint16_t flags = ReadU16(payload.data() + flagsOffset);
			constexpr uint16_t kHasFont = 0x0080;
			if ((flags & kHasFont) == 0) {
				return;
			}
			const size_t fontIdOffset = flagsOffset + 2;
			const uint16_t oldFontId = ReadU16(payload.data() + fontIdOffset);
			const uint16_t newFontId = FindRemappedCharacterId(remap, oldFontId);
			if (newFontId != oldFontId) {
				PatchU16(payload, fontIdOffset, newFontId);
			}
		}
		void AppendAs2ConstantPool(std::vector<uint8_t>& out, const char* value) {
			std::vector<uint8_t> payload;
			WriteU16(payload, 1);
			payload.insert(payload.end(), value, value + std::strlen(value));
			payload.push_back(0x00);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::ConstantPool));
			WriteU16(out, static_cast<uint16_t>(payload.size()));
			out.insert(out.end(), payload.begin(), payload.end());
		}

		void AppendAs2PushInt(std::vector<uint8_t>& out, int32_t number) {
			std::vector<uint8_t> payload;
			payload.push_back(0x07);
			WriteU32(payload, static_cast<uint32_t>(number));
			out.push_back(static_cast<uint8_t>(AS2ActionCode::Push));
			WriteU16(out, static_cast<uint16_t>(payload.size()));
			out.insert(out.end(), payload.begin(), payload.end());
		}

		void AppendAs2PushStringDirect(std::vector<uint8_t>& out, const char* value) {
			std::vector<uint8_t> payload;
			payload.push_back(0x00);
			payload.insert(payload.end(), value, value + std::strlen(value));
			payload.push_back(0x00);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::Push));
			WriteU16(out, static_cast<uint16_t>(payload.size()));
			out.insert(out.end(), payload.begin(), payload.end());
		}

		void AppendAs2CallFunctionOneInt(std::vector<uint8_t>& out, const char* functionName, int32_t arg) {
			AppendAs2PushInt(out, arg);
			AppendAs2PushInt(out, 1);
			AppendAs2PushStringDirect(out, functionName);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::CallFunction));
		}

		void AppendAs2CallFunctionNoArgs(std::vector<uint8_t>& out, const char* functionName) {
			AppendAs2PushInt(out, 0);
			AppendAs2PushStringDirect(out, functionName);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::CallFunction));
		}

		void AppendAs2CallFunctionOneString(std::vector<uint8_t>& out, const char* functionName, const char* arg) {
			AppendAs2PushStringDirect(out, arg);
			AppendAs2PushInt(out, 1);
			AppendAs2PushStringDirect(out, functionName);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::CallFunction));
		}

		void AppendAs2SetMemberInt(std::vector<uint8_t>& out, const char* objectName, const char* memberName, int32_t value) {
			AppendAs2PushStringDirect(out, objectName);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::GetVariable));
			AppendAs2PushStringDirect(out, memberName);
			AppendAs2PushInt(out, value);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::SetMember));
		}

		void AppendAs2SetNestedTextFromFunction(std::vector<uint8_t>& out, const char* rowName, const char* childName, const char* functionName, int32_t index) {
			AppendAs2PushStringDirect(out, rowName);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::GetVariable));
			AppendAs2PushStringDirect(out, childName);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::GetMember));
			AppendAs2PushStringDirect(out, "text");
			AppendAs2CallFunctionOneInt(out, functionName, index);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::SetMember));
		}

		void AppendAs2SetMemberFromFunction(std::vector<uint8_t>& out, const char* objectName, const char* memberName, const char* functionName, int32_t index) {
			AppendAs2PushStringDirect(out, objectName);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::GetVariable));
			AppendAs2PushStringDirect(out, memberName);
			AppendAs2CallFunctionOneInt(out, functionName, index);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::SetMember));
		}

		void AppendAs2CallNestedMethodWithFunctionResult(std::vector<uint8_t>& out, const char* objectName, const char* childName, const char* methodName, const char* functionName, int32_t index) {
			AppendAs2CallFunctionOneInt(out, functionName, index);
			AppendAs2PushInt(out, 1);
			AppendAs2PushStringDirect(out, objectName);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::GetVariable));
			AppendAs2PushStringDirect(out, childName);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::GetMember));
			AppendAs2PushStringDirect(out, methodName);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::CallMethod));
			out.push_back(static_cast<uint8_t>(AS2ActionCode::Pop));
		}

		void AppendAs2CallMethodWithNoArgFunctionResult(std::vector<uint8_t>& out, const char* objectName, const char* methodName, const char* functionName) {
			AppendAs2CallFunctionNoArgs(out, functionName);
			AppendAs2PushInt(out, 1);
			AppendAs2PushStringDirect(out, objectName);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::GetVariable));
			AppendAs2PushStringDirect(out, methodName);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::CallMethod));
			out.push_back(static_cast<uint8_t>(AS2ActionCode::Pop));
		}

		void AppendAs2DefineFunction(std::vector<uint8_t>& out, const char* functionName, const std::vector<uint8_t>& body) {
			std::vector<uint8_t> header;
			header.insert(header.end(), functionName, functionName + std::strlen(functionName));
			header.push_back(0x00);
			WriteU16(header, 0);
			WriteU16(header, static_cast<uint16_t>(body.size()));
			out.push_back(static_cast<uint8_t>(AS2ActionCode::DefineFunction));
			WriteU16(out, static_cast<uint16_t>(header.size()));
			out.insert(out.end(), header.begin(), header.end());
			out.insert(out.end(), body.begin(), body.end());
		}

		void AppendAs2InstallLobbySelectionUpdater(std::vector<uint8_t>& out) {
			constexpr const char* kFunctionName = "HookCrashersLobbyOverlayUpdate";
			std::vector<uint8_t> body;
			AppendAs2CallMethodWithNoArgFunctionResult(body, "baroverlayset", "gotoAndStop", "GetLobbySelectedFrame");
			AppendAs2DefineFunction(out, kFunctionName, body);

			AppendAs2PushStringDirect(out, "this");
			out.push_back(static_cast<uint8_t>(AS2ActionCode::GetVariable));
			AppendAs2PushStringDirect(out, "onEnterFrame");
			AppendAs2PushStringDirect(out, kFunctionName);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::GetVariable));
			out.push_back(static_cast<uint8_t>(AS2ActionCode::SetMember));

			AppendAs2PushInt(out, 0);
			AppendAs2PushStringDirect(out, kFunctionName);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::CallFunction));
			out.push_back(static_cast<uint8_t>(AS2ActionCode::Pop));
		}

		void AppendAs2RenderEmptyLobbyState(std::vector<uint8_t>& out) {
			AppendAs2SetMemberInt(out, "centertext", "_x", 400);
			AppendAs2SetMemberInt(out, "centertext", "_y", 300);

			AppendAs2PushStringDirect(out, "centertext");
			out.push_back(static_cast<uint8_t>(AS2ActionCode::GetVariable));
			AppendAs2PushStringDirect(out, "_visible");
			AppendAs2CallFunctionNoArgs(out, "GetLobbyCount");
			AppendAs2PushInt(out, 0);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::Equals2));
			out.push_back(static_cast<uint8_t>(AS2ActionCode::SetMember));

			AppendAs2PushStringDirect(out, "centertext");
			out.push_back(static_cast<uint8_t>(AS2ActionCode::GetVariable));
			AppendAs2PushStringDirect(out, "ntext");
			AppendAs2CallFunctionOneString(out, "GetLocalization", "hookcrashers.matchmaking.no_lobbies");
			out.push_back(static_cast<uint8_t>(AS2ActionCode::SetMember));
		}

		void AppendAs2RenderLobbyRows(std::vector<uint8_t>& out) {
			constexpr const char* rows[] = { "statusbar", "statusbar2", "statusbar3", "statusbar4" };
			for (int i = 0; i < 4; ++i) {
				AppendAs2SetMemberFromFunction(out, rows[i], "_visible", "GetLobbyVisible", i);
				AppendAs2SetNestedTextFromFunction(out, rows[i], "playername", "GetLobbyName", i);
				AppendAs2SetNestedTextFromFunction(out, rows[i], "playernumber", "GetLobbyPlayers", i);
				AppendAs2CallNestedMethodWithFunctionResult(out, rows[i], "ping", "gotoAndStop", "GetLobbyPingFrame", i);
			}
		}

		void AppendAs2PushConstant8(std::vector<uint8_t>& out, uint8_t index) {
			std::vector<uint8_t> payload;
			payload.push_back(0x08);
			payload.push_back(index);
			out.push_back(static_cast<uint8_t>(AS2ActionCode::Push));
			WriteU16(out, static_cast<uint16_t>(payload.size()));
			out.insert(out.end(), payload.begin(), payload.end());
		}
		void InsertStopActionOnFirstSpriteFrame(std::vector<uint8_t>& spritePayload, bool fetchLobbies) {
			if (spritePayload.size() < 6) {
				return;
			}

			const size_t tagStart = 4;
			size_t cursor = tagStart;
			while (cursor + 2 <= spritePayload.size()) {
				const uint16_t packed = ReadU16(spritePayload.data() + cursor);
				const uint16_t code = packed >> 6;
				uint32_t length = packed & 0x3F;
				size_t payloadOffset = cursor + 2;
				if (length == 0x3F) {
					if (cursor + 6 > spritePayload.size()) {
						return;
					}
					length = ReadU32(spritePayload.data() + cursor + 2);
					payloadOffset = cursor + 6;
				}

				const size_t next = payloadOffset + length;
				if (next > spritePayload.size()) {
					return;
				}

				if (code == static_cast<uint16_t>(SWFTagCode::ShowFrame)) {
					std::vector<uint8_t> actions;
					if (fetchLobbies) {
						AppendAs2ConstantPool(actions, "FetchLobbies");
						AppendAs2PushInt(actions, 0);
						AppendAs2PushConstant8(actions, 0);
						actions.push_back(static_cast<uint8_t>(AS2ActionCode::CallFunction));
						actions.push_back(static_cast<uint8_t>(AS2ActionCode::Pop));
						AppendAs2RenderEmptyLobbyState(actions);
						AppendAs2RenderLobbyRows(actions);
						AppendAs2InstallLobbySelectionUpdater(actions);
					}
					actions.push_back(static_cast<uint8_t>(AS2ActionCode::Stop));
					actions.push_back(0x00);

					std::vector<uint8_t> doAction;
					AppendTag(doAction, static_cast<uint16_t>(SWFTagCode::DoAction), actions);
					spritePayload.insert(spritePayload.begin() + static_cast<std::ptrdiff_t>(cursor), doAction.begin(), doAction.end());
					return;
				}

				if (code == static_cast<uint16_t>(SWFTagCode::End)) {
					return;
				}
				cursor = next;
			}
		}

		bool ShouldCopyImportedSwfTag(uint16_t code) {
			if (IsCharacterIdTag(code) || code == 13) {
				return true;
			}
			return false;
		}

		MenuSwfImportInjection BuildMatchmakingSwfInjection(const SWFBufferView& menuView, const uint8_t* menuPhysicalEnd, uint16_t minimumNextId) {
			MenuSwfImportInjection injection;
			SWFBufferView imported = MakeSWFBufferView(const_cast<uint8_t*>(kMatchmakingSwfData));
			if (!imported.valid || imported.compressed || imported.fileLength != kMatchmakingSwfSize || !imported.tags) {
				Util::Logger::Instance().Get()->warn(
					"[SWFInject] skip matchmaking import: invalid embedded SWF valid={} compressed={} length={} expected={}",
					imported.valid,
					imported.compressed,
					imported.fileLength,
					kMatchmakingSwfSize);
				return injection;
			}

			uint16_t nextId = FindNextAvailableCharacterId(menuView, menuPhysicalEnd, true);
			if (nextId < minimumNextId) {
				nextId = minimumNextId;
			}

			std::array<bool, 65536> importedCharacterIds{};
			const uint8_t* cursor = imported.tags;
			const uint8_t* end = imported.base + imported.fileLength;
			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					break;
				}
				if (IsCharacterIdTag(tag.code) && tag.length >= 2) {
					importedCharacterIds[ReadU16(tag.payload)] = true;
				}
				cursor = tag.next;
				if (tag.code == 0) {
					break;
				}
			}

			std::vector<std::pair<uint16_t, uint16_t>> remap;
			for (uint32_t oldId = 1; oldId < importedCharacterIds.size(); ++oldId) {
				if (!importedCharacterIds[oldId]) {
					continue;
				}
				if (nextId == 0 || nextId >= 65535) {
					Util::Logger::Instance().Get()->warn("[SWFInject] skip matchmaking import: no ids left next_id={}", nextId);
					return injection;
				}
				remap.emplace_back(static_cast<uint16_t>(oldId), nextId++);
			}

			cursor = imported.tags;
			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					break;
				}
				if (ShouldCopyImportedSwfTag(tag.code) && tag.length >= 2) {
					std::vector<uint8_t> payload(tag.payload, tag.payload + tag.length);
					const uint16_t oldId = ReadU16(payload.data());
					const uint16_t newId = FindRemappedCharacterId(remap, oldId);
					PatchU16(payload, 0, newId);

					if (tag.code == 39) {
						for (const auto& pair : remap) {
							PatchPlaceObjectCharacterReferences(payload, 4, payload.size(), pair.first, pair.second);
						}
						if (oldId == 12 || oldId == 15 || oldId == 23 || oldId == 26) {
							InsertStopActionOnFirstSpriteFrame(payload, oldId == 26);
						}
					}
					else if (tag.code == 37) {
						PatchDefineEditTextFontReference(payload, remap);
					}

					injection.definitions.push_back({ tag.code, std::move(payload) });
				}
				cursor = tag.next;
				if (tag.code == 0) {
					break;
				}
			}

			const uint16_t rootSpriteId = FindRemappedCharacterId(remap, 26);
			if (rootSpriteId == 26) {
				Util::Logger::Instance().Get()->warn("[SWFInject] skip matchmaking import: root sprite 26 was not remapped.");
				return injection;
			}
			injection.definitions.push_back({ 56, BuildExportAssetsPayload(rootSpriteId, "playerlobbies") });
			injection.exportedSpriteId = rootSpriteId;
			injection.nextCharacterId = nextId;
			injection.valid = true;
			Util::Logger::Instance().Get()->info(
				"[SWFInject] prepared matchmaking import root_old=26 root_new={} definitions={} remap_count={} next_id={}",
				rootSpriteId,
				injection.definitions.size(),
				remap.size(),
				nextId);
			return injection;
		}
		void AppendRawDefinitions(std::vector<uint8_t>& out, const std::vector<RawTagDefinition>& definitions) {
			for (const RawTagDefinition& definition : definitions) {
				AppendTag(out, definition.code, definition.payload);
			}
		}
		bool InsertDefinitionsAfterLastCharacterTag(std::vector<uint8_t>& swf, size_t headerSize, const InjectionSet& injections) {
			if (swf.size() < headerSize || injections.definitions.empty()) {
				return false;
			}

			const uint8_t* base = swf.data();
			const uint8_t* cursor = base + headerSize;
			const uint8_t* end = base + swf.size();
			size_t insertionOffset = swf.size();
			uint16_t lastCharacterId = 0;
			while (cursor < end) {
				Tag tag;
				if (!ReadTag(cursor, end, tag)) {
					break;
				}
				if (IsCharacterIdTag(tag.code) && tag.length >= 2) {
					lastCharacterId = ReadU16(tag.payload);
					insertionOffset = static_cast<size_t>(tag.next - base);
				}
				cursor = tag.next;
			}

			std::vector<uint8_t> tail;
			if (insertionOffset < swf.size()) {
				tail.assign(swf.begin() + static_cast<std::ptrdiff_t>(insertionOffset), swf.end());
				swf.resize(insertionOffset);
			}
			AppendDefinitions(swf, injections);
			if (!tail.empty()) {
				swf.insert(swf.end(), tail.begin(), tail.end());
			}
			Util::Logger::Instance().Get()->info(
				"[SWFInject] inserted portrait definitions after last character tag chid={} offset={}",
				lastCharacterId,
				insertionOffset);
			return insertionOffset < swf.size();
		}

		InjectionSet BuildInjectionSet(uint8_t* swfBuffer, const SWFBufferView& view, bool rawCharacterIdScan = false, const uint8_t* characterIdScanEnd = nullptr, uint16_t boundsTemplateShapeId = 153, bool useIconPortraits = false, bool useMapPortraits = false, uint16_t minimumNextId = 0, bool useFreshPortraitsOnly = false) {
			InjectionSet set;
			set.shapeTemplate151 = FindShapePayload(view, 151, &set.shapeTemplate151TagCode);
			uint16_t effectiveBoundsTemplateShapeId = boundsTemplateShapeId ? boundsTemplateShapeId : 153;
			set.shapeTemplate152 = FindShapePayload(view, effectiveBoundsTemplateShapeId, &set.shapeTemplate152TagCode);
			if (set.shapeTemplate152.empty() && effectiveBoundsTemplateShapeId != 153) {
				Util::Logger::Instance().Get()->warn(
					"[SWFInject] shape template {} missing; falling back to template 153.",
					effectiveBoundsTemplateShapeId);
				effectiveBoundsTemplateShapeId = 153;
				set.shapeTemplate152 = FindShapePayload(view, effectiveBoundsTemplateShapeId, &set.shapeTemplate152TagCode);
			}
			if (set.shapeTemplate152.size() > 2) {
				size_t rectLen = 0;
				if (DecodeRect(set.shapeTemplate152.data() + 2, set.shapeTemplate152.size() - 2, set.shapeTemplate152Bounds, rectLen)) {
					set.shapeTemplate152BoundsBytes.assign(set.shapeTemplate152.begin() + 2, set.shapeTemplate152.begin() + 2 + rectLen);
				}
			}
			LogShapeDiagnostics("vanilla_151", set.shapeTemplate151, set.shapeTemplate151TagCode);
			LogShapeDiagnostics("bounds_template", set.shapeTemplate152, set.shapeTemplate152TagCode);
			Util::Logger::Instance().Get()->debug(
				"[SWFInject] shape template {} found={} bytes={} bounds=[{},{} -> {},{}] bounds_bytes={}",
				effectiveBoundsTemplateShapeId,
				!set.shapeTemplate152.empty(),
				set.shapeTemplate152.size(),
				set.shapeTemplate152Bounds.xmin,
				set.shapeTemplate152Bounds.ymin,
				set.shapeTemplate152Bounds.xmax,
				set.shapeTemplate152Bounds.ymax,
				set.shapeTemplate152BoundsBytes.size());
			if (set.shapeTemplate152.empty()) {
				Util::Logger::Instance().Get()->warn("[SWFInject] skip portrait injection: shape template 153 not found.");
				return set;
			}

			std::array<bool, 65536> usedCharacterIds{};
			const uint8_t* idScanEnd = characterIdScanEnd ? characterIdScanEnd : view.base + view.fileLength;
			MarkKnownCharacterIds(view.tags, idScanEnd, usedCharacterIds);
			if (rawCharacterIdScan) {
				MarkKnownCharacterIdsPhysicalSequence(view.tags, idScanEnd, usedCharacterIds);
			}
			uint16_t maxCharacterId = 0;
			for (uint32_t id = 1; id < usedCharacterIds.size(); ++id) {
				if (usedCharacterIds[id]) {
					maxCharacterId = static_cast<uint16_t>(id);
				}
			}

			uint16_t nextId = static_cast<uint16_t>(std::max<uint32_t>(static_cast<uint32_t>(maxCharacterId) + 1u, minimumNextId));
			Util::Logger::Instance().Get()->debug(
				"[SWFInject] next imported portrait shape id={} max_character_id={} raw_scan={} scan_bytes={} minimum_next_id={}",
				nextId,
				maxCharacterId,
				rawCharacterIdScan,
				static_cast<size_t>(idScanEnd - view.base),
				minimumNextId);

			const auto& addons = Save::CharacterConfig::Instance().GetAddons();
			for (const auto& addon : addons) {
				if (useMapPortraits ? addon.portraitMapPath.empty() : (useFreshPortraitsOnly ? (addon.portraitFreshPath.empty() && addon.portraitClassicPath.empty()) : (useIconPortraits ? addon.portraitIconPath.empty() : (addon.portraitClassicPath.empty() && addon.portraitFreshPath.empty())))) {
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
					if (!EndsWithInsensitive(path, ".svg") && !EndsWithInsensitive(path, ".png") && !EndsWithInsensitive(path, ".swf")) {
						Util::Logger::Instance().Get()->warn("[SWFInject] portrait '{}' for character '{}' is not SVG/PNG/SWF.", path, addon.id);
						return {};
					}
					if (EndsWithInsensitive(path, ".swf")) {
						PortraitDefinition def;
						def.characterId = addon.id;
						def.sourcePath = path;
						def.shapeId = AllocateCharacterId(usedCharacterIds, nextId);
						if (!def.shapeId || !ExtractFirstDefineShapeFromSwf(path, def.shapeId, def.importedShapePayload, def.importedShapeTagCode)) {
							return {};
						}
						def.isImportedSwf = true;
						const PortraitPlacement placement{ def.shapeId };
						set.definitions.push_back(std::move(def));
						return placement;
					}
					if (EndsWithInsensitive(path, ".png")) {
						ImageRgba img;
						if (!DecodeImageWic(path, img)) {
							return {};
						}
						PortraitDefinition def;
						def.characterId = addon.id;
						def.sourcePath = path;
						def.shapeId = AllocateCharacterId(usedCharacterIds, nextId);
						def.bitmapId = AllocateCharacterId(usedCharacterIds, nextId);
						def.image = std::move(img);
						def.isSvg = false;
						const PortraitPlacement placement{ def.shapeId };
						set.definitions.push_back(std::move(def));
						return placement;
					}
					SvgDocument svg;
					if (!LoadSvgDocument(path, svg)) {
						Util::Logger::Instance().Get()->warn("[SWFInject] failed to parse SVG portrait '{}' for character '{}'.", path, addon.id);
						return {};
					}
					PortraitDefinition def;
					def.characterId = addon.id;
					def.sourcePath = path;
					def.shapeId = AllocateCharacterId(usedCharacterIds, nextId);
					def.svg = std::move(svg);
					def.isSvg = true;
					const PortraitPlacement placement{ def.shapeId };
					set.definitions.push_back(std::move(def));
					return placement;
				};

				const std::string classicPath = useMapPortraits ? addon.portraitMapPath : (useFreshPortraitsOnly ? std::string{} : (useIconPortraits ? addon.portraitIconPath : addon.portraitClassicPath));
				const std::string freshPath = useMapPortraits ? std::string{} : (useIconPortraits ? addon.portraitIconPath : (!addon.portraitFreshPath.empty() ? addon.portraitFreshPath : addon.portraitClassicPath));

				if (!addon.freshOnly && !classicPath.empty()) {
					const PortraitPlacement classicPlacement = define(classicPath);
					if (classicPlacement.shapeId) {
						set.classicPlacements.push_back(classicPlacement);
					}
				}

				if (!freshPath.empty()) {
					const PortraitPlacement freshPlacement = define(freshPath);
					if (freshPlacement.shapeId) {
						set.freshPlacements.push_back(freshPlacement);
					}
				}
			}
			return set;
		}
		bool PathContainsMain(const std::string& path) {
			std::string lower = path;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			return lower.find("main") != std::string::npos;
		}

		bool PathContainsClassicMap(const std::string& path) {
			std::string lower = path;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			return lower.find("map") != std::string::npos && lower.find("map_fresh") == std::string::npos;
		}

		bool PathContainsMapNewReplacement(const std::string& path) {
			std::string lower = path;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			return lower.find("map_new") != std::string::npos && lower.find("map_fresh") == std::string::npos;
		}

		void AppendAction(std::vector<uint8_t>& out, uint8_t code) {
			out.push_back(code);
		}

		void AppendActionWithPayload(std::vector<uint8_t>& out, uint8_t code, const std::vector<uint8_t>& payload) {
			out.push_back(code);
			WriteU16(out, static_cast<uint16_t>(payload.size()));
			out.insert(out.end(), payload.begin(), payload.end());
		}

		void AppendPushString(std::vector<uint8_t>& out, const char* value) {
			std::vector<uint8_t> payload;
			payload.push_back(0x00);
			payload.insert(payload.end(), value, value + std::strlen(value));
			payload.push_back(0x00);
			AppendActionWithPayload(out, static_cast<uint8_t>(AS2ActionCode::Push), payload);
		}

		void AppendPushStringInt(std::vector<uint8_t>& out, const char* value, int32_t number) {
			std::vector<uint8_t> payload;
			payload.push_back(0x00);
			payload.insert(payload.end(), value, value + std::strlen(value));
			payload.push_back(0x00);
			payload.push_back(0x07);
			WriteU32(payload, static_cast<uint32_t>(number));
			AppendActionWithPayload(out, static_cast<uint8_t>(AS2ActionCode::Push), payload);
		}

		void AppendPushStringDoubleString(std::vector<uint8_t>& out, const char* first, double number, const char* second) {
			std::vector<uint8_t> payload;
			payload.push_back(0x00);
			payload.insert(payload.end(), first, first + std::strlen(first));
			payload.push_back(0x00);
			payload.push_back(0x06);
			static_assert(sizeof(double) == 8);
			uint8_t raw[sizeof(double)] = {};
			std::memcpy(raw, &number, sizeof(double));
			payload.insert(payload.end(), raw, raw + sizeof(double));
			payload.push_back(0x00);
			payload.insert(payload.end(), second, second + std::strlen(second));
			payload.push_back(0x00);
			AppendActionWithPayload(out, static_cast<uint8_t>(AS2ActionCode::Push), payload);
		}

		void AppendSetMemberInt(std::vector<uint8_t>& body, const char* memberName, int32_t value) {
			AppendPushString(body, "save_data_info");
			AppendAction(body, static_cast<uint8_t>(AS2ActionCode::GetVariable));
			AppendPushStringInt(body, memberName, value);
			AppendAction(body, static_cast<uint8_t>(AS2ActionCode::SetMember));
		}

		void AppendSetVariableInt(std::vector<uint8_t>& body, const char* variableName, int32_t value) {
			AppendPushStringInt(body, variableName, value);
			AppendAction(body, static_cast<uint8_t>(AS2ActionCode::SetVariable));
		}

		std::vector<uint8_t> BuildInitSaveSystemBody() {
			const int addonCount = Save::CharacterConfig::Instance().GetAddonCount();
			std::vector<uint8_t> body;

			AppendPushStringDoubleString(body, "save_data_info", 0.0, "Object");
			AppendAction(body, static_cast<uint8_t>(AS2ActionCode::NewObject));
			AppendAction(body, static_cast<uint8_t>(AS2ActionCode::SetVariable));

			AppendSetMemberInt(body, "char_offset", Save::GLOBAL_UNLOCK_SIZE);
			AppendSetMemberInt(body, "char_size", Save::CHAR_DATA_SIZE);
			AppendSetMemberInt(body, "num_items", 128);
			AppendSetMemberInt(body, "num_animals", 32);
			AppendSetMemberInt(body, "num_levels", 64);
			AppendSetMemberInt(body, "num_relics", 8);
			AppendSetMemberInt(body, "num_items_expansion", 64);
			int classicAddonCount = 0;
			for (const auto& addon : Save::CharacterConfig::Instance().GetAddons()) {
				if (!addon.freshOnly) ++classicAddonCount;
			}
			AppendSetMemberInt(body, "num_characters_legacy", Save::BASE_CLASSIC_COUNT + classicAddonCount);
			AppendSetMemberInt(body, "num_characters_noaddons", Save::BASE_CHAR_COUNT + addonCount);
			AppendSetMemberInt(body, "num_characters_safe", Save::TOTAL_ORIGINAL_CHARS + addonCount);
			AppendSetMemberInt(body, "num_character_addons", Save::WORKSHOP_CHAR_COUNT);
			AppendSetMemberInt(body, "num_characters", Save::TOTAL_ORIGINAL_SLOTS + addonCount - 1);
			AppendSetVariableInt(body, "relic_offset", 40);
			AppendSetVariableInt(body, "weapon_offset", 50);
			return body;
		}

		std::vector<uint8_t> BuildInitSaveSystemDefineFunctionAction() {
			const std::vector<uint8_t> body = BuildInitSaveSystemBody();
			std::vector<uint8_t> header;
			constexpr const char* kFunctionName = "f_InitSaveSystem";
			header.insert(header.end(), kFunctionName, kFunctionName + std::strlen(kFunctionName));
			header.push_back(0x00);
			WriteU16(header, 0);
			WriteU16(header, static_cast<uint16_t>(body.size()));

			std::vector<uint8_t> action;
			AppendActionWithPayload(action, static_cast<uint8_t>(AS2ActionCode::DefineFunction), header);
			action.insert(action.end(), body.begin(), body.end());
			return action;
		}

		bool TryReadDefineFunctionHeader(const uint8_t* payload, uint16_t length, const char* wantedName, uint16_t& codeSize, size_t& headerSize) {
			codeSize = 0;
			headerSize = 0;
			if (!payload || !wantedName) {
				return false;
			}
			const uint8_t* end = payload + length;
			const uint8_t* nameEnd = static_cast<const uint8_t*>(std::memchr(payload, 0, length));
			if (!nameEnd || nameEnd + 5 > end) {
				return false;
			}
			const size_t wantedLen = std::strlen(wantedName);
			if (static_cast<size_t>(nameEnd - payload) != wantedLen || std::memcmp(payload, wantedName, wantedLen) != 0) {
				return false;
			}

			const uint8_t* cursor = nameEnd + 1;
			const uint16_t numParams = ReadU16(cursor);
			cursor += 2;
			for (uint16_t i = 0; i < numParams; ++i) {
				const uint8_t* paramEnd = static_cast<const uint8_t*>(std::memchr(cursor, 0, static_cast<size_t>(end - cursor)));
				if (!paramEnd) {
					return false;
				}
				cursor = paramEnd + 1;
			}
			if (cursor + 2 > end) {
				return false;
			}
			codeSize = ReadU16(cursor);
			cursor += 2;
			headerSize = static_cast<size_t>(cursor - payload);
			return true;
		}

		bool PatchInitSaveSystemActions(const uint8_t* actions, uint32_t length, std::vector<uint8_t>& out, int& patchedCount) {
			const uint8_t* cursor = actions;
			const uint8_t* end = actions + length;
			while (cursor < end) {
				const uint8_t actionCode = *cursor++;
				if (actionCode == static_cast<uint8_t>(AS2ActionCode::End)) {
					out.push_back(actionCode);
					out.insert(out.end(), cursor, end);
					return patchedCount > 0;
				}

				if (actionCode < 0x80) {
					out.push_back(actionCode);
					continue;
				}

				if (cursor + 2 > end) {
					return patchedCount > 0;
				}
				const uint16_t actionLength = ReadU16(cursor);
				const uint8_t* actionLengthPtr = cursor;
				cursor += 2;
				if (cursor + actionLength > end) {
					return patchedCount > 0;
				}

				uint16_t defineCodeSize = 0;
				size_t defineHeaderSize = 0;
				if (actionCode == static_cast<uint8_t>(AS2ActionCode::DefineFunction)
					&& TryReadDefineFunctionHeader(cursor, actionLength, "f_InitSaveSystem", defineCodeSize, defineHeaderSize)) {
					const std::vector<uint8_t> replacement = BuildInitSaveSystemDefineFunctionAction();
					out.insert(out.end(), replacement.begin(), replacement.end());
					++patchedCount;

					const bool bodyStoredAfterAction = defineHeaderSize == actionLength && cursor + actionLength + defineCodeSize <= end;
					cursor += actionLength + (bodyStoredAfterAction ? defineCodeSize : 0);
					continue;
				}
				else {
					out.push_back(actionCode);
					out.push_back(actionLengthPtr[0]);
					out.push_back(actionLengthPtr[1]);
					out.insert(out.end(), cursor, cursor + actionLength);
				}
				cursor += actionLength;
			}
			return patchedCount > 0;
		}
	}

	bool TryPatchDisplayObjectSortDepth(void* displayObject, int depthValue) {
		if (!displayObject) {
			return false;
		}
		int* sortDepth = reinterpret_cast<int*>(reinterpret_cast<uint8_t*>(displayObject) + 0x20);
		if (*sortDepth == depthValue) {
			return false;
		}
		*sortDepth = depthValue;
		return true;
	}

	bool TryPatchMainSaveSystem(SWFScene* scene, const std::string& swfPath) {
		if (!scene || !scene->swfBuffer) {
			return false;
		}
		if (!PathContainsMain(swfPath)) {
			return false;
		}

		SWFBufferView view = MakeSWFBufferView(scene->swfBuffer);
		if (!view.valid || view.compressed || !view.tags || view.fileLength < view.headerSize) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip main save-system patch: invalid SWF view valid={} compressed={} tags={} file_length={} header_size={} path='{}'.",
				view.valid,
				view.compressed,
				static_cast<const void*>(view.tags),
				view.fileLength,
				view.headerSize,
				swfPath);
			return false;
		}

		std::vector<uint8_t> patched;
		patched.reserve(view.fileLength + 512);
		patched.insert(patched.end(), view.base, view.tags);

		int patchedActions = 0;
		const uint8_t* cursor = view.tags;
		const uint8_t* end = view.base + view.fileLength;
		while (cursor < end) {
			Tag tag;
			if (!ReadTag(cursor, end, tag)) {
				Util::Logger::Instance().Get()->warn("[SWFInject] skip main save-system patch: failed to read root tag at offset {}.", static_cast<size_t>(cursor - view.base));
				return false;
			}

			if (tag.code == 0) {
				AppendTag(patched, 0, std::vector<uint8_t>{});
				break;
			}

			if (tag.code == static_cast<uint16_t>(SWFTagCode::DoAction)) {
				std::vector<uint8_t> actionPayload;
				PatchInitSaveSystemActions(tag.payload, tag.length, actionPayload, patchedActions);
				if (!actionPayload.empty()) {
					AppendTag(patched, tag.code, actionPayload);
				}
				else {
					patched.insert(patched.end(), tag.begin, tag.next);
				}
			}
			else if (tag.code == static_cast<uint16_t>(SWFTagCode::DoInitAction) && tag.length >= 2) {
				std::vector<uint8_t> actionPayload;
				actionPayload.push_back(tag.payload[0]);
				actionPayload.push_back(tag.payload[1]);
				PatchInitSaveSystemActions(tag.payload + 2, tag.length - 2, actionPayload, patchedActions);
				AppendTag(patched, tag.code, actionPayload);
			}
			else {
				patched.insert(patched.end(), tag.begin, tag.next);
			}
			cursor = tag.next;
		}

		if (patchedActions <= 0) {
			Util::Logger::Instance().Get()->warn("[SWFInject] main save-system patch skipped: f_InitSaveSystem not found path='{}'.", swfPath);
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
		Util::Logger::Instance().Get()->info("[SWFInject] patched main f_InitSaveSystem actions={} old_size={} new_size={} addon_count={}.",
			patchedActions,
			view.fileLength,
			newLength,
			Save::CharacterConfig::Instance().GetAddonCount());
		return true;
	}
	bool TryInjectMainPortraitIcons(SWFScene* scene, const std::string& swfPath) {
		if (!scene || !scene->swfBuffer) {
			return false;
		}
		if (!PathContainsMain(swfPath)) {
			return false;
		}

		SWFBufferView view = MakeSWFBufferView(scene->swfBuffer);
		if (!view.valid || view.compressed || !view.tags || view.fileLength < view.headerSize) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip main icon patch: invalid SWF view valid={} compressed={} tags={} file_length={} header_size={} path='{}'.",
				view.valid,
				view.compressed,
				static_cast<const void*>(view.tags),
				view.fileLength,
				view.headerSize,
				swfPath);
			return false;
		}

		InjectionSet injections = BuildInjectionSet(scene->swfBuffer, view, false, nullptr, 153, true);
		if (injections.classicPlacements.empty() && injections.freshPlacements.empty()) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip main icon patch: no portrait_icon frames prepared addons={} definitions={} classic_frames={} fresh_frames={}",
				Save::CharacterConfig::Instance().GetAddons().size(),
				injections.definitions.size(),
				injections.classicPlacements.size(),
				injections.freshPlacements.size());
			return false;
		}

		constexpr uint16_t kMainPortraitIconSpriteId = 167;
		const uint8_t* physicalEnd = view.base + view.fileLength;
		Tag spriteTag;
		if (!FindDefineSpriteTagByIdRaw(view.tags, physicalEnd, kMainPortraitIconSpriteId, spriteTag)) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip main icon patch: sprite={} not found file_length={}",
				kMainPortraitIconSpriteId,
				view.fileLength);
			return false;
		}

		std::vector<uint8_t> spritePayload;
		if (!PatchMainPortraitIconSprite(spriteTag.payload, spriteTag.length, injections, spritePayload, kMainPortraitIconSpriteId)) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip main icon patch: failed to patch sprite={} offset={} length={}",
				kMainPortraitIconSpriteId,
				static_cast<size_t>(spriteTag.begin - view.base),
				spriteTag.length);
			return false;
		}

		const uint8_t* rootEndTag = physicalEnd;
		if (view.fileLength >= 2 && ReadU16(physicalEnd - 2) == 0) {
			rootEndTag = physicalEnd - 2;
		}

		std::vector<uint8_t> patched;
		patched.reserve(view.fileLength + spritePayload.size() + injections.definitions.size() * 256 * 1024);
		patched.insert(patched.end(), static_cast<const uint8_t*>(view.base), rootEndTag);

		std::vector<uint8_t> patchedSpriteTag;
		AppendTag(patchedSpriteTag, 39, spritePayload);
		const size_t spriteOffset = static_cast<size_t>(spriteTag.begin - view.base);
		const size_t oldSpriteBytes = static_cast<size_t>(spriteTag.next - spriteTag.begin);
		if (spriteOffset + oldSpriteBytes > patched.size()) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip main icon patch: sprite={} replacement range invalid offset={} old_bytes={} patched_size={}",
				kMainPortraitIconSpriteId,
				spriteOffset,
				oldSpriteBytes,
				patched.size());
			return false;
		}
		patched.erase(patched.begin() + static_cast<std::ptrdiff_t>(spriteOffset), patched.begin() + static_cast<std::ptrdiff_t>(spriteOffset + oldSpriteBytes));
		patched.insert(patched.begin() + static_cast<std::ptrdiff_t>(spriteOffset), patchedSpriteTag.begin(), patchedSpriteTag.end());

		const size_t definitionsOffset = patched.size();
		AppendDefinitions(patched, injections);
		patched.insert(patched.end(), rootEndTag, physicalEnd);

		if (patched.size() < sizeof(SWFHeader) || patched.size() > 64u * 1024u * 1024u) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip main icon patch: patched_size={} min_size={} max_size={}",
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
			"[SWFInject] patched main portrait icon sprite={} definitions={} classic_frames={} fresh_frames={} old_size={} new_size={} definitions_offset={}",
			kMainPortraitIconSpriteId,
			injections.definitions.size(),
			injections.classicPlacements.size(),
			injections.freshPlacements.size(),
			view.fileLength,
			newLength,
			definitionsOffset);
		return true;
	}
	bool TryInjectMapPortraits(SWFScene* scene, const std::string& swfPath) {
		if (!scene) {
			return false;
		}
		if (!PathContainsMapNewReplacement(swfPath)) {
			return false;
		}
		DeleteDiagnosticDump("map_new_patched.swf");

		std::vector<uint8_t> replacement(kMapNewSwfData, kMapNewSwfData + kMapNewSwfSize);
		SWFBufferView view = MakeSWFBufferView(replacement.data());
		if (!view.valid || view.compressed || !view.tags || view.fileLength < view.headerSize) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip map_new replacement: invalid embedded SWF valid={} compressed={} tags={} file_length={} header_size={} path='{}'.",
				view.valid,
				view.compressed,
				static_cast<const void*>(view.tags),
				view.fileLength,
				view.headerSize,
				swfPath);
			return false;
		}

		constexpr uint16_t kMapNewFreshSpriteId = 752;
		constexpr size_t kMapNewFreshTemplateFrameIndex = 134; // frame 135.
		const uint8_t* physicalEnd = view.base + view.fileLength;
		Tag spriteTag;
		if (!FindDefineSpriteTagByIdRaw(view.tags, physicalEnd, kMapNewFreshSpriteId, spriteTag)) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip map_new replacement: sprite={} not found file_length={} path='{}'.",
				kMapNewFreshSpriteId,
				view.fileLength,
				swfPath);
			return false;
		}

		uint16_t mapFreshTemplateShapeId = 0;
		if (!FindFrameDepth1PlaceObject2CharacterId(spriteTag.payload, spriteTag.length, kMapNewFreshTemplateFrameIndex, &mapFreshTemplateShapeId)) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip map_new replacement: sprite={} frame=135 has no PlaceObject2 depth=1 template character.",
				kMapNewFreshSpriteId);
			return false;
		}

		InjectionSet injections = BuildInjectionSet(replacement.data(), view, false, nullptr, mapFreshTemplateShapeId, false, false, 0, true);

		std::vector<uint8_t> spritePayload;
		if (!PatchMapNewFreshPortraitSprite(spriteTag.payload, spriteTag.length, injections, spritePayload, kMapNewFreshSpriteId)) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip map_new replacement: failed to patch sprite={} offset={} length={}",
				kMapNewFreshSpriteId,
				static_cast<size_t>(spriteTag.begin - view.base),
				spriteTag.length);
			return false;
		}

		const uint8_t* rootEndTag = physicalEnd;
		if (view.fileLength >= 2 && ReadU16(physicalEnd - 2) == 0) {
			rootEndTag = physicalEnd - 2;
		}

		std::vector<uint8_t> patched;
		patched.reserve(view.fileLength + spritePayload.size() + injections.definitions.size() * 256 * 1024);
		patched.insert(patched.end(), static_cast<const uint8_t*>(view.base), rootEndTag);

		std::vector<uint8_t> patchedSpriteTag;
		AppendTag(patchedSpriteTag, 39, spritePayload);
		const size_t spriteOffset = static_cast<size_t>(spriteTag.begin - view.base);
		const size_t oldSpriteBytes = static_cast<size_t>(spriteTag.next - spriteTag.begin);
		if (spriteOffset + oldSpriteBytes > patched.size()) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip map_new replacement: sprite={} replacement range invalid offset={} old_bytes={} patched_size={}",
				kMapNewFreshSpriteId,
				spriteOffset,
				oldSpriteBytes,
				patched.size());
			return false;
		}
		patched.erase(patched.begin() + static_cast<std::ptrdiff_t>(spriteOffset), patched.begin() + static_cast<std::ptrdiff_t>(spriteOffset + oldSpriteBytes));
		patched.insert(patched.begin() + static_cast<std::ptrdiff_t>(spriteOffset), patchedSpriteTag.begin(), patchedSpriteTag.end());

		const size_t definitionsOffset = patched.size();
		AppendDefinitions(patched, injections);
		patched.insert(patched.end(), rootEndTag, physicalEnd);

		if (patched.size() < sizeof(SWFHeader) || patched.size() > 64u * 1024u * 1024u) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip map_new replacement: patched_size={} min_size={} max_size={}",
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
		const size_t finalPatchedSize = owned->size();
		WriteDiagnosticDump("map_new_patched.swf", scene->swfBuffer, finalPatchedSize);
		g_ownedPatchedBuffers.push_back(std::move(owned));
		Util::Logger::Instance().Get()->info(
			"[SWFInject] replaced map_new with embedded SWF sprite={} template_frame=135 template_shape={} definitions={} fresh_frames={} old_size={} new_size={} definitions_offset={} path='{}'",
			kMapNewFreshSpriteId,
			mapFreshTemplateShapeId,
			injections.definitions.size(),
			injections.freshPlacements.size(),
			view.fileLength,
			newLength,
			definitionsOffset,
			swfPath);
		return true;
	}

	bool TryInjectLobbyPortraits(SWFScene* scene, const std::string& swfPath) {
		if (!scene || !scene->swfBuffer) {
			return false;
		}
		if (!PathContainsLobby(swfPath)) {
			return false;
		}
		Util::Logger::Instance().Get()->debug("[SWFInject] lobby candidate path='{}' scene={} swf={}.",
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
		Util::Logger::Instance().Get()->debug(
			"[SWFInject] prepared lobby portraits definitions={} classic_frames={} fresh_frames={} file_length={} header_size={}.",
			injections.definitions.size(),
			injections.classicPlacements.size(),
			injections.freshPlacements.size(),
			view.fileLength,
			view.headerSize);

		std::vector<uint8_t> patched;
		patched.reserve(view.fileLength + injections.definitions.size() * 512 * 1024);
		patched.insert(patched.end(), view.base, view.tags);

		bool configuredSpriteExists = false;
		{
			const uint8_t* scan = view.tags;
			const uint8_t* scanEnd = view.base + view.fileLength;
			while (scan < scanEnd) {
				Tag scanTag;
				if (!ReadTag(scan, scanEnd, scanTag)) {
					break;
				}
				if (scanTag.code == 39 && scanTag.length >= 4 && ReadU16(scanTag.payload) == kLobbyPortraitSpriteId) {
					configuredSpriteExists = true;
					break;
				}
				if (scanTag.code == 0) {
					break;
				}
				scan = scanTag.next;
			}
		}
		if (!configuredSpriteExists) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] configured lobby portrait sprite {} not present in root tags; fallback discovery enabled.",
				kLobbyPortraitSpriteId);
		}

		Util::Logger::Instance().Get()->debug(
			"[SWFInject] imported portrait definitions will be appended at the end of the root tag list, immediately before End.");
		bool patchedSprite = false;
		bool appendedDefinitions = false;
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
				if (!appendedDefinitions) {
					AppendDefinitions(patched, injections);
					appendedDefinitions = true;
					Util::Logger::Instance().Get()->debug("[SWFInject] appended imported portrait definitions before root End tag.");
				}
				AppendTag(patched, 0, std::vector<uint8_t>{});
				break;
			}

			if (!patchedSprite && tag.code == 39 && tag.length >= 4) {
				const uint16_t spriteId = ReadU16(tag.payload);
				const uint16_t frameCount = ReadU16(tag.payload + 2);
				const bool isConfiguredSprite = spriteId == kLobbyPortraitSpriteId;
				const bool isFallbackCandidate = false;
				if (isConfiguredSprite || isFallbackCandidate) {
					if (isFallbackCandidate) {
						Util::Logger::Instance().Get()->warn(
							"[SWFInject] trying DefineSprite {} as lobby portrait fallback for missing configured sprite {} frame_count={}.",
							spriteId,
							kLobbyPortraitSpriteId,
							frameCount);
					}
					std::vector<uint8_t> spritePayload;
					if (PatchLobbyPortraitSprite(tag.payload, tag.length, injections, spritePayload, spriteId)) {
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
		Util::Logger::Instance().Get()->debug(
			"[SWFInject] patched lobby portraits definitions={} classic_frames={} fresh_frames={} old_size={} new_size={}.",
			injections.definitions.size(),
			injections.classicPlacements.size(),
			injections.freshPlacements.size(),
			view.fileLength,
			newLength);
		return true;
			}	bool TryInjectMenuPortraits(SWFScene* scene, const std::string& swfPath) {
		if (!scene || !scene->swfBuffer) {
			return false;
		}
		if (!PathContainsMenu(swfPath)) {
			return false;
		}

		SWFBufferView view = MakeSWFBufferView(scene->swfBuffer);
		if (!view.valid || view.compressed || !view.tags || view.fileLength < view.headerSize) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip menu patch: invalid SWF view path='{}' valid={} compressed={} tags={} file_length={} header_size={}.",
				swfPath,
				view.valid,
				view.compressed,
				static_cast<const void*>(view.tags),
				view.fileLength,
				view.headerSize);
			return false;
		}

		size_t menuTailTagOffset = view.fileLength;
		const size_t physicalCopyLength = ComputeMenuPhysicalCopyLength(view, &menuTailTagOffset);
		if (physicalCopyLength < view.headerSize || physicalCopyLength > 64u * 1024u * 1024u) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip menu patch: invalid physical length path='{}' declared={} physical={} tail_offset={}.",
				swfPath,
				view.fileLength,
				physicalCopyLength,
				menuTailTagOffset);
			return false;
		}
		const uint8_t* physicalEnd = view.base + physicalCopyLength;
		WriteDiagnosticDump("menu_original.swf", static_cast<const uint8_t*>(view.base), physicalCopyLength);

		MenuOnlineCompatibilityInjection onlineCompatibility = BuildMenuOnlineCompatibilityInjection(view, physicalEnd);
		MenuSwfImportInjection matchmaking = BuildMatchmakingSwfInjection(view, physicalEnd, onlineCompatibility.nextCharacterId);
		g_injectedMatchmakingMenuSpriteId = matchmaking.valid ? matchmaking.exportedSpriteId : 0;
		const uint16_t minimumPortraitId = matchmaking.valid ? matchmaking.nextCharacterId : onlineCompatibility.nextCharacterId;
		InjectionSet injections = BuildInjectionSet(scene->swfBuffer, view, true, physicalEnd, 326, false, false, minimumPortraitId);
		if (injections.classicPlacements.empty() && injections.freshPlacements.empty() && !onlineCompatibility.valid && !matchmaking.valid) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip menu patch: no portrait frames prepared addons={} definitions={} classic_frames={} fresh_frames={}",
				Save::CharacterConfig::Instance().GetAddons().size(),
				injections.definitions.size(),
				injections.classicPlacements.size(),
				injections.freshPlacements.size());
			return false;
		}

		constexpr uint16_t kMenuPortraitSpriteId = 331;
		Tag spriteTag;
		if (!FindDefineSpriteTagByIdRaw(view.tags, physicalEnd, kMenuPortraitSpriteId, spriteTag)) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip menu patch: sprite={} not found physical_length={} declared_length={}",
				kMenuPortraitSpriteId,
				physicalCopyLength,
				view.fileLength);
			return false;
		}

		std::vector<uint8_t> spritePayload;
		if (!PatchMenuPortraitSprite(spriteTag.payload, spriteTag.length, injections, spritePayload, kMenuPortraitSpriteId)) {
			Util::Logger::Instance().Get()->debug(
				"[SWFInject] skip menu patch: failed to patch sprite={} offset={} length={} physical_length={}",
				kMenuPortraitSpriteId,
				static_cast<size_t>(spriteTag.begin - view.base),
				spriteTag.length,
				physicalCopyLength);
			return false;
		}

		const uint8_t* physicalBase = static_cast<const uint8_t*>(view.base);
		const uint8_t* rootEndTag = physicalEnd;
		if (physicalCopyLength >= 2 && ReadU16(physicalEnd - 2) == 0) {
			rootEndTag = physicalEnd - 2;
		}

		std::vector<uint8_t> patched;
		patched.reserve(physicalCopyLength + spritePayload.size() + onlineCompatibility.definitions.size() * 4096 + matchmaking.definitions.size() * 4096 + injections.definitions.size() * 512 * 1024);
		patched.insert(patched.end(), physicalBase, rootEndTag);

		std::vector<uint8_t> patchedSpriteTag;
		AppendTag(patchedSpriteTag, 39, spritePayload);
		const size_t spriteOffset = static_cast<size_t>(spriteTag.begin - physicalBase);
		const size_t oldSpriteBytes = static_cast<size_t>(spriteTag.next - spriteTag.begin);
		if (spriteOffset + oldSpriteBytes > patched.size()) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip menu patch: sprite={} replacement range invalid offset={} old_bytes={} patched_size={}",
				kMenuPortraitSpriteId,
				spriteOffset,
				oldSpriteBytes,
				patched.size());
			return false;
		}
		patched.erase(patched.begin() + static_cast<std::ptrdiff_t>(spriteOffset), patched.begin() + static_cast<std::ptrdiff_t>(spriteOffset + oldSpriteBytes));
		patched.insert(patched.begin() + static_cast<std::ptrdiff_t>(spriteOffset), patchedSpriteTag.begin(), patchedSpriteTag.end());

		const size_t onlineCompatibilityDefinitionsOffset = patched.size();
		if (onlineCompatibility.valid) {
			AppendRawDefinitions(patched, onlineCompatibility.definitions);
		}
		const size_t matchmakingDefinitionsOffset = patched.size();
		if (matchmaking.valid) {
			AppendRawDefinitions(patched, matchmaking.definitions);
		}
		const size_t definitionsOffset = patched.size();
		AppendDefinitions(patched, injections);
		patched.insert(patched.end(), rootEndTag, physicalEnd);

		if (patched.size() < sizeof(SWFHeader) || patched.size() > 64u * 1024u * 1024u) {
			Util::Logger::Instance().Get()->warn(
				"[SWFInject] skip menu patch: patched_size={} min_size={} max_size={}.",
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

		WriteDiagnosticDump("menu_patched.swf", patched.data(), patched.size());
		auto owned = std::make_unique<std::vector<uint8_t>>(std::move(patched));
		scene->swfBuffer = owned->data();
		g_ownedPatchedBuffers.push_back(std::move(owned));
		Util::Logger::Instance().Get()->info(
			"[SWFInject] patched menu portrait sprite={} in_place=true onlinecompatibility={} online_sprite={} online_defs={} matchmaking={} matchmaking_sprite={} matchmaking_defs={} portrait_definitions={} classic_frames={} fresh_frames={} declared={} physical={} new_size={} online_offset={} matchmaking_offset={} definitions_offset={}",
			kMenuPortraitSpriteId,
			onlineCompatibility.valid,
			onlineCompatibility.exportedSpriteId,
			onlineCompatibility.definitions.size(),
			matchmaking.valid,
				matchmaking.exportedSpriteId,
				matchmaking.definitions.size(),
				injections.definitions.size(),
			injections.classicPlacements.size(),
			injections.freshPlacements.size(),
			view.fileLength,
			physicalCopyLength,
			newLength,
			onlineCompatibilityDefinitionsOffset,
			matchmakingDefinitionsOffset,
			definitionsOffset);
		return true;
	}

	uint16_t GetInjectedMatchmakingMenuSpriteId() {
		return g_injectedMatchmakingMenuSpriteId;
	}

}














