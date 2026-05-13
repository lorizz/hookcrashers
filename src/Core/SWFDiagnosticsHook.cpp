#include "SWFDiagnosticsHook.h"

#include "../Util/Logger.h"
#include "../SWF/AS2NativeRegistry.h"
#include "../SWF/SWFBytecode.h"
#include "../SWF/SWFStructures.h"

#include <windows.h>
#include <detours.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace HookCrashers::Core {
	namespace {
		constexpr uintptr_t kSWFMovieCtorRva = 0x11EC70; // VA 0x8FEC70 with IDA base 0x7E0000.
		constexpr uintptr_t kSWFSceneParseHeaderRva = 0x1232C0; // VA 0x9032C0 with image base 0x7E0000.
		constexpr uintptr_t kSWFNodeCtorLoadCOK6Rva = 0xFB7A0; // VA 0x8DB7A0 with IDA base 0x7E0000.
		constexpr int kSwfDumpBytes = 64;

		using SWFMovieCtorFn = int(__thiscall*)(int thisPtr, int a2, int* name, int swfBuffer, char flags);
		using SWFSceneParseHeaderFn = int(__thiscall*)(int scene);
		using SWFNodeCtorLoadCOK6Fn = char*(__thiscall*)(char* thisPtr, int a2, int pathObject, char flags);
		SWFMovieCtorFn g_originalSWFMovieCtor = nullptr;
		SWFSceneParseHeaderFn g_originalSWFSceneParseHeader = nullptr;
		SWFNodeCtorLoadCOK6Fn g_originalSWFNodeCtorLoadCOK6 = nullptr;
		thread_local std::string g_currentSWFPath;

		std::string HexDump(const uint8_t* data, int maxBytes) {
			if (!data || maxBytes <= 0) {
				return "<null>";
			}

			std::ostringstream stream;
			stream << std::hex << std::setfill('0');
			for (int i = 0; i < maxBytes; ++i) {
				if (i) {
					stream << ' ';
				}
				stream << std::setw(2) << static_cast<int>(data[i]);
			}
			return stream.str();
		}

		uint32_t ReadU32LE(const uint8_t* data) {
			if (!data) {
				return 0;
			}
			return static_cast<uint32_t>(data[0])
				| (static_cast<uint32_t>(data[1]) << 8)
				| (static_cast<uint32_t>(data[2]) << 16)
				| (static_cast<uint32_t>(data[3]) << 24);
		}

		bool IsLikelyReadableGamePointer(uint32_t value) {
			return value >= 0x10000 && value < 0x80000000;
		}

		std::string EscapeAscii(const char* text, int length) {
			if (!text || length <= 0) {
				return "";
			}

			std::string result;
			result.reserve(static_cast<size_t>(length));
			for (int i = 0; i < length; ++i) {
				const unsigned char ch = static_cast<unsigned char>(text[i]);
				if (ch == 0) {
					break;
				}
				if (ch >= 0x20 && ch <= 0x7E) {
					result.push_back(static_cast<char>(ch));
				}
				else {
					result.push_back('.');
				}
			}
			return result;
		}

		std::string ScanInlineAscii(const char* text, int maxLength) {
			if (!text || maxLength <= 0) {
				return "";
			}

			std::string best;
			std::string current;
			for (int i = 0; i < maxLength; ++i) {
				const unsigned char ch = static_cast<unsigned char>(text[i]);
				if (ch >= 0x20 && ch <= 0x7E) {
					current.push_back(static_cast<char>(ch));
				}
				else {
					if (current.size() > best.size()) {
						best = current;
					}
					current.clear();
				}
			}
			if (current.size() > best.size()) {
				best = current;
			}
			return best;
		}

		std::string DescribeStringObject(int* value) {
			if (!value) {
				return "name=<null>";
			}

			const uint32_t* raw = reinterpret_cast<const uint32_t*>(value);
			std::ostringstream stream;
			stream << "name=0x" << std::hex << reinterpret_cast<uintptr_t>(value)
				<< " raw=["
				<< raw[0] << ' ' << raw[1] << ' ' << raw[2] << ' ' << raw[3] << ' '
				<< raw[4] << ' ' << raw[5] << ' ' << raw[6] << ' ' << raw[7] << ']';

			const int lenA = static_cast<int>(raw[2]);
			const int capA = static_cast<int>(raw[3]);
			if (lenA >= 0 && lenA < 260 && capA >= 0 && capA < 1024) {
				const char* text = nullptr;
				if (capA >= 16 && IsLikelyReadableGamePointer(raw[0])) {
					text = reinterpret_cast<const char*>(raw[0]);
				}
				else {
					text = reinterpret_cast<const char*>(value);
				}
				stream << " strA='" << EscapeAscii(text, lenA) << "' lenA=" << std::dec << lenA << " capA=" << capA;
			}

			const int lenB = static_cast<int>(raw[3]);
			const int capB = static_cast<int>(raw[4]);
			if (lenB >= 0 && lenB < 260 && capB >= 0 && capB < 1024) {
				const char* text = nullptr;
				if (capB >= 16 && IsLikelyReadableGamePointer(raw[1])) {
					text = reinterpret_cast<const char*>(raw[1]);
				}
				else {
					text = reinterpret_cast<const char*>(value);
				}
				stream << " strB='" << EscapeAscii(text, lenB) << "' lenB=" << lenB << " capB=" << capB;
			}

			const std::string inlineAscii = ScanInlineAscii(reinterpret_cast<const char*>(value), 64);
			if (inlineAscii.size() >= 3) {
				stream << " inline='" << inlineAscii << "'";
			}
			if (IsLikelyReadableGamePointer(raw[0])) {
				const std::string ptr0Ascii = ScanInlineAscii(reinterpret_cast<const char*>(raw[0]), 128);
				if (ptr0Ascii.size() >= 3) {
					stream << " ptr0='" << ptr0Ascii << "'";
				}
			}
			if (IsLikelyReadableGamePointer(raw[1])) {
				const std::string ptr1Ascii = ScanInlineAscii(reinterpret_cast<const char*>(raw[1]), 128);
				if (ptr1Ascii.size() >= 3) {
					stream << " ptr1='" << ptr1Ascii << "'";
				}
			}

			return stream.str();
		}

		std::string BestStringGuess(int* value) {
			if (!value) {
				return "";
			}

			const uint32_t* raw = reinterpret_cast<const uint32_t*>(value);
			const int lenA = static_cast<int>(raw[2]);
			const int capA = static_cast<int>(raw[3]);
			if (lenA > 0 && lenA < 260 && capA >= 0 && capA < 1024) {
				const char* text = capA >= 16 && IsLikelyReadableGamePointer(raw[0])
					? reinterpret_cast<const char*>(raw[0])
					: reinterpret_cast<const char*>(value);
				const std::string guess = EscapeAscii(text, lenA);
				if (!guess.empty()) {
					return guess;
				}
			}

			const int lenB = static_cast<int>(raw[3]);
			const int capB = static_cast<int>(raw[4]);
			if (lenB > 0 && lenB < 260 && capB >= 0 && capB < 1024) {
				const char* text = capB >= 16 && IsLikelyReadableGamePointer(raw[1])
					? reinterpret_cast<const char*>(raw[1])
					: reinterpret_cast<const char*>(value);
				const std::string guess = EscapeAscii(text, lenB);
				if (!guess.empty()) {
					return guess;
				}
			}

			std::string guess = ScanInlineAscii(reinterpret_cast<const char*>(value), 64);
			if (guess.size() >= 3) {
				return guess;
			}
			if (IsLikelyReadableGamePointer(raw[0])) {
				guess = ScanInlineAscii(reinterpret_cast<const char*>(raw[0]), 128);
				if (guess.size() >= 3) {
					return guess;
				}
			}
			if (IsLikelyReadableGamePointer(raw[1])) {
				guess = ScanInlineAscii(reinterpret_cast<const char*>(raw[1]), 128);
				if (guess.size() >= 3) {
					return guess;
				}
			}
			return "";
		}

		std::string DescribeSWFBuffer(int swfBuffer) {
			uint8_t* swf = reinterpret_cast<uint8_t*>(swfBuffer);
			HookCrashers::SWF::SWFBufferView view = HookCrashers::SWF::MakeSWFBufferView(swf);
			if (!view.valid) {
				return "swf=<null>";
			}

			std::ostringstream stream;
			stream << "swf=0x" << std::hex << swfBuffer
				<< " sig='" << view.header->signature[0] << view.header->signature[1] << view.header->signature[2] << "'"
				<< std::dec << " version=" << static_cast<int>(view.header->version)
				<< " declared_length=" << view.fileLength
				<< " header_size=" << static_cast<int>(view.headerSize)
				<< " frame_rate_raw=" << view.parsedFrameRateRaw
				<< " frame_count=" << view.parsedFrameCount
				<< " tags=0x" << std::hex << reinterpret_cast<uintptr_t>(view.tags)
				<< " head=[" << HexDump(swf, kSwfDumpBytes) << "]";
			return stream.str();
		}

		std::string DescribeSceneAfterCtor(int thisPtr) {
			if (!thisPtr) {
				return "movie=<null>";
			}

			const uint8_t* movie = reinterpret_cast<const uint8_t*>(thisPtr);
			const uint32_t scene = *reinterpret_cast<const uint32_t*>(movie + 88);
			if (!scene) {
				return "scene=<null>";
			}

			const HookCrashers::SWF::SWFScene* scenePtr = reinterpret_cast<const HookCrashers::SWF::SWFScene*>(scene);
			const uint32_t sceneSwf = reinterpret_cast<uint32_t>(scenePtr->swfBuffer);
			const uint32_t parseOffset = *reinterpret_cast<const uint32_t*>(movie + 120);
			const uint32_t dataPtr = reinterpret_cast<uint32_t>(scenePtr->tagStream);

			std::ostringstream stream;
			stream << "scene=0x" << std::hex << scene
				<< " scene_swf=0x" << sceneSwf
				<< " data_ptr=0x" << dataPtr
				<< std::dec
				<< " parse_offset=" << parseOffset
				<< " signature=" << static_cast<int>(scenePtr->signature)
				<< " version=" << static_cast<int>(scenePtr->version)
				<< " declared_length=" << scenePtr->fileLength
				<< " frame_count=" << scenePtr->frameCount
				<< " header_size=" << static_cast<int>(scenePtr->headerSize);
			return stream.str();
		}

		std::string DescribeSceneBeforeParse(int scene) {
			if (!scene) {
				return "scene=<null>";
			}

			const HookCrashers::SWF::SWFScene* scenePtr = reinterpret_cast<const HookCrashers::SWF::SWFScene*>(scene);
			const uint32_t swfBuffer = reinterpret_cast<uint32_t>(scenePtr->swfBuffer);

			std::ostringstream stream;
			stream << "scene=0x" << std::hex << scene << " " << DescribeSWFBuffer(static_cast<int>(swfBuffer));
			return stream.str();
		}

		std::string DescribeSceneAfterParse(int scene, int result) {
			if (!scene) {
				return "scene=<null>";
			}

			const HookCrashers::SWF::SWFScene* scenePtr = reinterpret_cast<const HookCrashers::SWF::SWFScene*>(scene);
			const uint32_t sceneSwf = reinterpret_cast<uint32_t>(scenePtr->swfBuffer);
			const uint32_t dataPtr = reinterpret_cast<uint32_t>(scenePtr->tagStream);

			std::ostringstream stream;
			stream << "scene=0x" << std::hex << scene
				<< " scene_swf=0x" << sceneSwf
				<< " data_ptr=0x" << dataPtr
				<< std::dec
				<< " result=" << result
				<< " signature=" << static_cast<int>(scenePtr->signature)
				<< " version=" << static_cast<int>(scenePtr->version)
				<< " declared_length=" << scenePtr->fileLength
				<< " frame_count=" << scenePtr->frameCount
				<< " header_size=" << static_cast<int>(scenePtr->headerSize);
			return stream.str();
		}

		bool ContainsInterestingToken(std::string_view value) {
			return value.find("AttachSkinGraphic") != std::string_view::npos
				|| value.find("LobbySkinCharAvail") != std::string_view::npos
				|| value.find("LobbyTrySelectChar") != std::string_view::npos
				|| value.find("LobbyTryReadySkins") != std::string_view::npos
				|| value.find("IsCharacterUnlockedForPlayer") != std::string_view::npos
				|| value.find("GetPlayerCharacterType") != std::string_view::npos
				|| value.find("SetLocalPlayerCharacterType") != std::string_view::npos
				|| value.find("portrait") != std::string_view::npos
				|| value.find("selectscreen") != std::string_view::npos
				|| value.find("char") != std::string_view::npos
				|| value.find("skin") != std::string_view::npos;
		}

		std::string ReadNullTerminatedString(uint8_t*& cursor, uint8_t* end) {
			uint8_t* start = cursor;
			while (cursor < end && *cursor) {
				++cursor;
			}
			if (cursor >= end) {
				return {};
			}
			std::string value(reinterpret_cast<const char*>(start), reinterpret_cast<const char*>(cursor));
			++cursor;
			return value;
		}

		struct AS2ScanStats {
			int actionCount = 0;
			int constantPoolCount = 0;
			int pushCount = 0;
			int callFunctionCount = 0;
			int callMethodCount = 0;
			int interestingStringCount = 0;
		};

		void ScanAS2Actions(uint8_t* actions, uint32_t length, const char* owner, int depth, AS2ScanStats& stats) {
			if (!actions || !length) {
				return;
			}

			uint8_t* cursor = actions;
			uint8_t* end = actions + length;
			std::vector<std::string> constantPool;

			while (cursor < end) {
				const uint32_t actionOffset = static_cast<uint32_t>(cursor - actions);
				const uint8_t actionCode = *cursor++;
				++stats.actionCount;
				if (actionCode == 0) {
					break;
				}

				uint16_t actionLength = 0;
				if (actionCode >= 0x80) {
					if (cursor + 2 > end) {
						break;
					}
					actionLength = HookCrashers::SWF::ReadU16LE(cursor);
					cursor += 2;
				}
				uint8_t* payload = cursor;
				uint8_t* payloadEnd = cursor + actionLength;
				if (payloadEnd > end) {
					break;
				}

				switch (static_cast<HookCrashers::SWF::AS2ActionCode>(actionCode)) {
				case HookCrashers::SWF::AS2ActionCode::ConstantPool: {
					++stats.constantPoolCount;
					if (payload + 2 <= payloadEnd) {
						const uint16_t count = HookCrashers::SWF::ReadU16LE(payload);
						uint8_t* poolCursor = payload + 2;
						constantPool.clear();
						for (uint16_t i = 0; i < count && poolCursor < payloadEnd; ++i) {
							std::string value = ReadNullTerminatedString(poolCursor, payloadEnd);
							constantPool.push_back(value);
							if (ContainsInterestingToken(value)) {
								++stats.interestingStringCount;
								Util::Logger::Instance().Get()->info(
									"[SWFDiag][AS2] {} depth={} action=0x{:X} ConstantPool[{}]='{}'.",
									owner,
									depth,
									actionOffset,
									i,
									value);
							}
						}
					}
					break;
				}
				case HookCrashers::SWF::AS2ActionCode::Push: {
					++stats.pushCount;
					uint8_t* pushCursor = payload;
					while (pushCursor < payloadEnd) {
						const uint8_t type = *pushCursor++;
						if (type == 0) {
							std::string value = ReadNullTerminatedString(pushCursor, payloadEnd);
							if (ContainsInterestingToken(value)) {
								++stats.interestingStringCount;
								Util::Logger::Instance().Get()->info(
									"[SWFDiag][AS2] {} depth={} action=0x{:X} PushString='{}'.",
									owner,
									depth,
									actionOffset,
									value);
							}
						}
						else if (type == 8) {
							if (pushCursor >= payloadEnd) {
								break;
							}
							const uint8_t index = *pushCursor++;
							if (index < constantPool.size() && ContainsInterestingToken(constantPool[index])) {
								++stats.interestingStringCount;
								Util::Logger::Instance().Get()->info(
									"[SWFDiag][AS2] {} depth={} action=0x{:X} PushConst8[{}]='{}'.",
									owner,
									depth,
									actionOffset,
									static_cast<int>(index),
									constantPool[index]);
							}
						}
						else if (type == 9) {
							if (pushCursor + 2 > payloadEnd) {
								break;
							}
							const uint16_t index = HookCrashers::SWF::ReadU16LE(pushCursor);
							pushCursor += 2;
							if (index < constantPool.size() && ContainsInterestingToken(constantPool[index])) {
								++stats.interestingStringCount;
								Util::Logger::Instance().Get()->info(
									"[SWFDiag][AS2] {} depth={} action=0x{:X} PushConst16[{}]='{}'.",
									owner,
									depth,
									actionOffset,
									index,
									constantPool[index]);
							}
						}
						else if (type == 1) {
							pushCursor += 4;
						}
						else if (type == 2 || type == 3) {
						}
						else if (type == 4 || type == 5) {
							pushCursor += 1;
						}
						else if (type == 6) {
							pushCursor += 8;
						}
						else if (type == 7) {
							pushCursor += 4;
						}
						else {
							break;
						}
					}
					break;
				}
				case HookCrashers::SWF::AS2ActionCode::CallFunction:
					++stats.callFunctionCount;
					break;
				case HookCrashers::SWF::AS2ActionCode::CallMethod:
					++stats.callMethodCount;
					break;
				default:
					break;
				}

				cursor = payloadEnd;
			}
		}

		void ScanSWFTags(uint8_t* start, uint8_t* end, const char* owner, int depth, int& tagCount, int& actionBlockCount) {
			uint8_t* cursor = start;
			while (cursor && cursor < end) {
				HookCrashers::SWF::SWFTagHeader tag;
				if (!HookCrashers::SWF::ReadSWFTagHeader(cursor, end, tag)) {
					Util::Logger::Instance().Get()->warn("[SWFDiag][Tags] {} depth={} malformed tag at offset=0x{:X}.", owner, depth, static_cast<uint32_t>(cursor - start));
					break;
				}

				++tagCount;
				if (tag.code == static_cast<uint16_t>(HookCrashers::SWF::SWFTagCode::End)) {
					break;
				}

				if (tag.code == static_cast<uint16_t>(HookCrashers::SWF::SWFTagCode::DoAction)) {
					++actionBlockCount;
					AS2ScanStats stats;
					ScanAS2Actions(tag.payload, tag.length, owner, depth, stats);
					if (stats.interestingStringCount > 0) {
						Util::Logger::Instance().Get()->info(
							"[SWFDiag][AS2] {} depth={} DoAction len={} actions={} pools={} pushes={} calls={} methods={} interesting={}.",
							owner,
							depth,
							tag.length,
							stats.actionCount,
							stats.constantPoolCount,
							stats.pushCount,
							stats.callFunctionCount,
							stats.callMethodCount,
							stats.interestingStringCount);
					}
				}
				else if (tag.code == static_cast<uint16_t>(HookCrashers::SWF::SWFTagCode::DoInitAction)) {
					if (tag.length > 2) {
						++actionBlockCount;
						AS2ScanStats stats;
						ScanAS2Actions(tag.payload + 2, tag.length - 2, owner, depth, stats);
						if (stats.interestingStringCount > 0) {
							Util::Logger::Instance().Get()->info(
								"[SWFDiag][AS2] {} depth={} DoInitAction sprite={} len={} actions={} interesting={}.",
								owner,
								depth,
								HookCrashers::SWF::ReadU16LE(tag.payload),
								tag.length,
								stats.actionCount,
								stats.interestingStringCount);
						}
					}
				}
				else if (tag.code == static_cast<uint16_t>(HookCrashers::SWF::SWFTagCode::DefineSprite) && depth < 2 && tag.length > 4) {
					const uint16_t spriteId = HookCrashers::SWF::ReadU16LE(tag.payload);
					const uint16_t frameCount = HookCrashers::SWF::ReadU16LE(tag.payload + 2);
					int nestedTags = 0;
					int nestedActions = 0;
					ScanSWFTags(tag.payload + 4, tag.payload + tag.length, owner, depth + 1, nestedTags, nestedActions);
					if (nestedActions > 0) {
						Util::Logger::Instance().Get()->info(
							"[SWFDiag][Tags] {} depth={} DefineSprite id={} frames={} nested_tags={} nested_action_blocks={}.",
							owner,
							depth,
							spriteId,
							frameCount,
							nestedTags,
							nestedActions);
					}
				}

				cursor = tag.next;
			}
		}

		void ScanSWFBytecode(uint8_t* swfBuffer, const std::string& path) {
			HookCrashers::SWF::SWFBufferView view = HookCrashers::SWF::MakeSWFBufferView(swfBuffer);
			if (!view.valid || view.compressed || !view.tags || !view.fileLength) {
				return;
			}

			uint8_t* end = view.base + view.fileLength;
			int tagCount = 0;
			int actionBlockCount = 0;
			ScanSWFTags(view.tags, end, path.empty() ? "<unknown>" : path.c_str(), 0, tagCount, actionBlockCount);
			Util::Logger::Instance().Get()->info(
				"[SWFDiag][Tags] scanned path='{}' tags={} action_blocks={} length={}.",
				path,
				tagCount,
				actionBlockCount,
				view.fileLength);
		}

		enum class SWFDumpTarget {
			None,
			Menu,
			Lobby,
		};

		SWFDumpTarget DetectDumpTarget(const std::string& path) {
			if (path.find("menu") != std::string::npos || path.find("Menu") != std::string::npos) {
				return SWFDumpTarget::Menu;
			}
			if (path.find("lobby") != std::string::npos || path.find("Lobby") != std::string::npos) {
				return SWFDumpTarget::Lobby;
			}
			return SWFDumpTarget::None;
		}

		const char* DumpTargetName(SWFDumpTarget target) {
			switch (target) {
			case SWFDumpTarget::Menu:
				return "menu";
			case SWFDumpTarget::Lobby:
				return "lobby";
			default:
				return "unknown";
			}
		}

		const char* DumpTargetFileName(SWFDumpTarget target) {
			switch (target) {
			case SWFDumpTarget::Menu:
				return "menu.swf";
			case SWFDumpTarget::Lobby:
				return "lobby.swf";
			default:
				return "unknown.swf";
			}
		}

		void DumpSWFBufferToFile(uint8_t* swfBuffer, const std::string& path) {
			HookCrashers::SWF::SWFBufferView view = HookCrashers::SWF::MakeSWFBufferView(swfBuffer);
			if (!view.valid || !view.fileLength || view.fileLength > 64u * 1024u * 1024u) {
				return;
			}
			const SWFDumpTarget target = DetectDumpTarget(path);
			if (target != SWFDumpTarget::Lobby) {
				return;
			}

			static bool dumpedLobby = false;
			if (dumpedLobby) {
				return;
			}
			dumpedLobby = true;

			char modulePath[MAX_PATH] = {};
			GetModuleFileNameA(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
			char* lastSlash = strrchr(modulePath, '\\');
			if (lastSlash) {
				*(lastSlash + 1) = '\0';
			}

			char dumpDir[MAX_PATH] = {};
			sprintf_s(dumpDir, "%sHookCrashersDumps", modulePath);
			if (!CreateDirectoryA(dumpDir, nullptr)) {
				const DWORD error = GetLastError();
				if (error != ERROR_ALREADY_EXISTS) {
					Util::Logger::Instance().Get()->error(
						"[SWFInject] failed to create dump dir '{}' error={}.",
						dumpDir,
						error);
					return;
				}
			}

			char outputPath[MAX_PATH] = {};
			sprintf_s(outputPath, "%s\\lobby_patched.swf", dumpDir);

			FILE* file = nullptr;
			if (fopen_s(&file, outputPath, "wb") != 0 || !file) {
				Util::Logger::Instance().Get()->error("[SWFInject] failed to open patched lobby dump '{}' errno={}.", outputPath, errno);
				return;
			}

			const size_t written = fwrite(swfBuffer, 1, view.fileLength, file);
			fclose(file);
			if (written != view.fileLength) {
				Util::Logger::Instance().Get()->error(
					"[SWFInject] short write patched lobby dump '{}' bytes={} written={} source_path='{}'.",
					outputPath,
					view.fileLength,
					written,
					path);
				return;
			}
			Util::Logger::Instance().Get()->info(
				"[SWFInject] wrote patched lobby dump '{}' bytes={} written={} source_path='{}'.",
				outputPath,
				view.fileLength,
				written,
				path);
		}
	}

	int __fastcall DetouredSWFMovieCtor(int thisPtr, void*, int a2, int* name, int swfBuffer, char flags) {
		return g_originalSWFMovieCtor(thisPtr, a2, name, swfBuffer, flags);
	}

	char* __fastcall DetouredSWFNodeCtorLoadCOK6(char* thisPtr, void*, int a2, int pathObject, char flags) {
		const std::string previousPath = g_currentSWFPath;
		g_currentSWFPath = BestStringGuess(reinterpret_cast<int*>(pathObject));
		char* result = g_originalSWFNodeCtorLoadCOK6(thisPtr, a2, pathObject, flags);
		g_currentSWFPath = previousPath;
		return result;
	}

	int __fastcall DetouredSWFSceneParseHeader(int scene, void*) {
		return g_originalSWFSceneParseHeader(scene);
	}

	bool SetupSWFDiagnosticsHook(uintptr_t moduleBase) {
		if (!moduleBase) {
			return false;
		}

		g_originalSWFMovieCtor = reinterpret_cast<SWFMovieCtorFn>(moduleBase + kSWFMovieCtorRva);
		g_originalSWFSceneParseHeader = reinterpret_cast<SWFSceneParseHeaderFn>(moduleBase + kSWFSceneParseHeaderRva);
		g_originalSWFNodeCtorLoadCOK6 = reinterpret_cast<SWFNodeCtorLoadCOK6Fn>(moduleBase + kSWFNodeCtorLoadCOK6Rva);

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		LONG attachResult = DetourAttach(&(PVOID&)g_originalSWFNodeCtorLoadCOK6, DetouredSWFNodeCtorLoadCOK6);
		if (attachResult != NO_ERROR) {
			Util::Logger::Instance().Get()->error("[SWFInject] SWFNode_Ctor_LoadCOK6 DetourAttach failed: {}.", attachResult);
			DetourTransactionAbort();
			return false;
		}
		attachResult = DetourAttach(&(PVOID&)g_originalSWFMovieCtor, DetouredSWFMovieCtor);
		if (attachResult != NO_ERROR) {
			Util::Logger::Instance().Get()->error("[SWFInject] SWFMovie_Ctor DetourAttach failed: {}.", attachResult);
			DetourTransactionAbort();
			return false;
		}
		attachResult = DetourAttach(&(PVOID&)g_originalSWFSceneParseHeader, DetouredSWFSceneParseHeader);
		if (attachResult != NO_ERROR) {
			Util::Logger::Instance().Get()->error("[SWFInject] SWFScene_ParseHeader DetourAttach failed: {}.", attachResult);
			DetourTransactionAbort();
			return false;
		}

		LONG commitResult = DetourTransactionCommit();
		if (commitResult != NO_ERROR) {
			Util::Logger::Instance().Get()->error("[SWFInject] SWF detour transaction commit failed: {}.", commitResult);
			return false;
		}
		return true;
	}
}
