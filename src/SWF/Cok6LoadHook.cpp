#include "Cok6LoadHook.h"

#include "InitSaveSystemBytecode.h"
#include "ShapeInjection.h"
#include "MenuGraphicAssets.h"
#include "OnlineMenuAssets.h"
#include "SWFStructures.h"
#include "../Characters/CharacterRegistry.h"
#include "../Core/Logger.h"
#include "../NativeDB/Natives.h"
#include <windows.h>
#include <detours.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace HookCrashers::SWF {
namespace {

#ifndef HC_ENABLE_LOBBY_COK6_RUNTIME_DUMP
#define HC_ENABLE_LOBBY_COK6_RUNTIME_DUMP 1
#endif

constexpr std::uint16_t kTagEnd = 0;
constexpr std::uint16_t kTagDoAction = 12;
constexpr std::uint16_t kTagDoInitAction = 59;
constexpr std::uint16_t kTagShowFrame = 1;
constexpr std::uint16_t kTagDefineSprite = 39;
constexpr std::uint8_t kActionEnd = 0x00;
constexpr std::uint8_t kActionPop = 0x17;
constexpr std::uint8_t kActionPush = 0x96;
constexpr std::uint8_t kActionStoreRegister = 0x87;
constexpr std::uint8_t kActionDefineFunction2 = 0x8E;
constexpr std::uint8_t kActionDefineFunction = 0x9B;
constexpr std::uint16_t kLobbyPortraitSpriteId = 180;
constexpr int kClassicCloneSourceFrame = 34;
constexpr int kFreshCloneSourceFrame = 135;

using SWFSceneParseHeaderFn = int(__thiscall*)(int scene);
using SWFNodeCtorLoadCOK6Fn = char*(__thiscall*)(char* thisPtr, int a2, int pathObject, char flags);

SWFSceneParseHeaderFn g_OriginalSWFSceneParseHeader = nullptr;
SWFNodeCtorLoadCOK6Fn g_OriginalSWFNodeCtorLoadCOK6 = nullptr;
thread_local std::string g_CurrentCok6Path;
std::atomic<bool> g_CharacterPatchesEnabled {false};
using OwnedPatchedBuffers = std::vector<std::unique_ptr<std::vector<std::uint8_t>>>;
OwnedPatchedBuffers g_OwnedPatchedBuffers;
std::mutex g_OwnedPatchedBuffersMutex;
volatile LONG g_LobbyRuntimeDumpIndex = 0;

struct Tag {
    const std::uint8_t* begin = nullptr;
    const std::uint8_t* payload = nullptr;
    const std::uint8_t* next = nullptr;
    std::uint32_t length = 0;
    std::uint16_t code = 0;
};

std::uint16_t ReadU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) | static_cast<std::uint16_t>(p[1] << 8);
}

std::uint32_t ReadU32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0])
        | (static_cast<std::uint32_t>(p[1]) << 8)
        | (static_cast<std::uint32_t>(p[2]) << 16)
        | (static_cast<std::uint32_t>(p[3]) << 24);
}

void WriteU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void WriteU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

bool ReadTag(const std::uint8_t* cursor, const std::uint8_t* end, Tag& tag) {
    if (!cursor || cursor + 2 > end) {
        return false;
    }

    const std::uint8_t* start = cursor;
    const std::uint16_t packed = ReadU16(cursor);
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

void AppendTag(std::vector<std::uint8_t>& out, std::uint16_t code, const std::uint8_t* payload, std::uint32_t length) {
    WriteU16(out, static_cast<std::uint16_t>((code << 6) | (length >= 0x3F ? 0x3F : length)));
    if (length >= 0x3F) {
        WriteU32(out, length);
    }
    if (payload && length) {
        out.insert(out.end(), payload, payload + length);
    }
}

void AppendTag(std::vector<std::uint8_t>& out, std::uint16_t code, const std::vector<std::uint8_t>& payload) {
    AppendTag(out, code, payload.data(), static_cast<std::uint32_t>(payload.size()));
}

bool IsLikelyReadableGamePointer(std::uint32_t value) {
    return value >= 0x10000 && value < 0x80000000;
}

std::string EscapeAscii(const char* text, int length) {
    std::string result;
    if (!text || length <= 0) {
        return result;
    }
    result.reserve(static_cast<std::size_t>(length));
    for (int i = 0; i < length; ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch == 0) {
            break;
        }
        result.push_back(ch >= 0x20 && ch <= 0x7E ? static_cast<char>(ch) : '.');
    }
    return result;
}

std::string ScanInlineAscii(const char* text, int maxLength) {
    std::string best;
    std::string current;
    if (!text || maxLength <= 0) {
        return best;
    }
    for (int i = 0; i < maxLength; ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch >= 0x20 && ch <= 0x7E) {
            current.push_back(static_cast<char>(ch));
        } else {
            if (current.size() > best.size()) {
                best = current;
            }
            current.clear();
        }
    }
    return current.size() > best.size() ? current : best;
}

std::string BestStringGuess(int* value) {
    if (!value) {
        return {};
    }

    const auto* raw = reinterpret_cast<const std::uint32_t*>(value);
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
    return {};
}

bool PathContainsMain(std::string path) {
    std::transform(path.begin(), path.end(), path.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return path.find("main") != std::string::npos;
}

bool PathContainsLobby(std::string path) {
    std::transform(path.begin(), path.end(), path.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return path.find("lobby") != std::string::npos;
}

std::string RuntimeDumpDirectory() {
    return Core::Logger::ModuleDirectory() + "\\diagnostics\\swf_runtime";
}

void EnsureDirectoryExists(const std::string& path) {
    if (path.empty()) {
        return;
    }

    std::string partial;
    partial.reserve(path.size());
    for (char ch : path) {
        partial.push_back(ch);
        if (ch == '\\' || ch == '/') {
            if (partial.size() > 3) {
                CreateDirectoryA(partial.c_str(), nullptr);
            }
        }
    }
    CreateDirectoryA(path.c_str(), nullptr);
}

void WriteTextFile(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file) {
        Core::Logger::Error("Lobby runtime dump failed to write text '%s'", path.c_str());
        return;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string BuildTagSummary(const SWFBufferView& view) {
    char line[256] {};
    std::string text;
    std::snprintf(line,
                  sizeof(line),
                  "signature=%c%c%c version=%u length=%u headerSize=%u frameCount=%u frameRateRaw=0x%04X compressed=%d\n",
                  view.header ? view.header->signature[0] : '?',
                  view.header ? view.header->signature[1] : '?',
                  view.header ? view.header->signature[2] : '?',
                  view.header ? static_cast<unsigned>(view.header->version) : 0,
                  static_cast<unsigned>(view.fileLength),
                  static_cast<unsigned>(view.headerSize),
                  static_cast<unsigned>(view.parsedFrameCount),
                  static_cast<unsigned>(view.parsedFrameRateRaw),
                  view.compressed ? 1 : 0);
    text += line;

    if (!view.valid || view.compressed || !view.tags || view.fileLength < view.headerSize) {
        text += "tags=unavailable\n";
        return text;
    }

    const std::uint8_t* cursor = view.tags;
    const std::uint8_t* end = view.base + view.fileLength;
    int index = 0;
    int tag142Count = 0;
    int tag148Count = 0;
    while (cursor < end) {
        Tag tag;
        if (!ReadTag(cursor, end, tag)) {
            std::snprintf(line, sizeof(line), "tag[%d]=parse_error offset=0x%08X\n", index, static_cast<unsigned>(cursor - view.base));
            text += line;
            break;
        }

        if (tag.code == 142) {
            ++tag142Count;
        } else if (tag.code == 148) {
            ++tag148Count;
        }

        if (tag.code == 142 || tag.code == 148 || tag.code == kTagDefineSprite || tag.code == kTagDoAction || tag.code == kTagDoInitAction) {
            std::snprintf(line,
                          sizeof(line),
                          "tag[%d] code=%u offset=0x%08X payload=0x%08X length=%u\n",
                          index,
                          static_cast<unsigned>(tag.code),
                          static_cast<unsigned>(tag.begin - view.base),
                          static_cast<unsigned>(tag.payload - view.base),
                          static_cast<unsigned>(tag.length));
            text += line;
        }

        cursor = tag.next;
        ++index;
        if (tag.code == kTagEnd) {
            break;
        }
    }

    std::snprintf(line, sizeof(line), "tag_count=%d castle_tag142=%d castle_tag148=%d\n", index, tag142Count, tag148Count);
    text += line;
    return text;
}

void DumpLobbyRuntimeCok6(SWFScene* scene, const std::string& swfPath, const char* stage) {
#if HC_ENABLE_LOBBY_COK6_RUNTIME_DUMP
    if (!scene || !scene->swfBuffer || !PathContainsLobby(swfPath)) {
        return;
    }

    SWFBufferView view = MakeSWFBufferView(scene->swfBuffer);
    if (!view.valid || view.fileLength == 0 || view.fileLength > 64u * 1024u * 1024u) {
        Core::Logger::Warn("Lobby runtime dump skipped stage='%s' valid=%d length=%u path='%s'",
                           stage ? stage : "?",
                           view.valid ? 1 : 0,
                           static_cast<unsigned>(view.fileLength),
                           swfPath.c_str());
        return;
    }

    const LONG dumpIndex = InterlockedIncrement(&g_LobbyRuntimeDumpIndex);
    const std::string directory = RuntimeDumpDirectory();
    EnsureDirectoryExists(directory);

    char baseName[96] {};
    std::snprintf(baseName, sizeof(baseName), "lobby_runtime_%03ld_%s", dumpIndex, stage ? stage : "unknown");
    const std::string swfOutPath = directory + "\\" + baseName + ".swf";
    const std::string txtOutPath = directory + "\\" + baseName + ".txt";

    std::ofstream swfOut(swfOutPath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!swfOut) {
        Core::Logger::Error("Lobby runtime dump failed to open '%s'", swfOutPath.c_str());
        return;
    }
    swfOut.write(reinterpret_cast<const char*>(view.base), static_cast<std::streamsize>(view.fileLength));
    swfOut.close();

    std::string summary = "path=" + swfPath + "\n";
    summary += "stage=" + std::string(stage ? stage : "unknown") + "\n";
    summary += BuildTagSummary(view);
    WriteTextFile(txtOutPath, summary);

    Core::Logger::Warn("Diagnostic lobby.cok6 runtime dump wrote '%s' bytes=%u summary='%s'",
                       swfOutPath.c_str(),
                       static_cast<unsigned>(view.fileLength),
                       txtOutPath.c_str());
#else
    (void)scene;
    (void)swfPath;
    (void)stage;
#endif
}

std::vector<std::uint8_t> BuildDefineInitSaveSystemAction() {
    const std::vector<std::uint8_t> body = BuildInitSaveSystemBytecode();
    constexpr const char* kName = "f_InitSaveSystem";

    std::vector<std::uint8_t> header;
    header.insert(header.end(), kName, kName + std::strlen(kName));
    header.push_back(0x00);
    WriteU16(header, 0);
    WriteU16(header, static_cast<std::uint16_t>(body.size()));

    std::vector<std::uint8_t> action;
    action.push_back(kActionDefineFunction);
    WriteU16(action, static_cast<std::uint16_t>(header.size()));
    action.insert(action.end(), header.begin(), header.end());
    action.insert(action.end(), body.begin(), body.end());
    return action;
}

bool TryReadDefineFunctionHeader(const std::uint8_t* payload, std::uint16_t length, const char* wantedName, std::uint16_t& codeSize, std::size_t& headerSize) {
    codeSize = 0;
    headerSize = 0;
    if (!payload || !wantedName) {
        return false;
    }

    const std::uint8_t* end = payload + length;
    const auto* nameEnd = static_cast<const std::uint8_t*>(std::memchr(payload, 0, length));
    if (!nameEnd || nameEnd + 5 > end) {
        return false;
    }
    const std::size_t wantedLen = std::strlen(wantedName);
    if (static_cast<std::size_t>(nameEnd - payload) != wantedLen || std::memcmp(payload, wantedName, wantedLen) != 0) {
        return false;
    }

    const std::uint8_t* cursor = nameEnd + 1;
    const std::uint16_t numParams = ReadU16(cursor);
    cursor += 2;
    for (std::uint16_t i = 0; i < numParams; ++i) {
        const auto* paramEnd = static_cast<const std::uint8_t*>(std::memchr(cursor, 0, static_cast<std::size_t>(end - cursor)));
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
    headerSize = static_cast<std::size_t>(cursor - payload);
    return true;
}

bool PatchInitSaveSystemActions(const std::uint8_t* actions, std::uint32_t length, std::vector<std::uint8_t>& out, int& patchedCount) {
    const std::uint8_t* cursor = actions;
    const std::uint8_t* end = actions + length;
    while (cursor < end) {
        const std::uint8_t actionCode = *cursor++;
        if (actionCode == kActionEnd) {
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
        const std::uint16_t actionLength = ReadU16(cursor);
        const std::uint8_t* actionLengthPtr = cursor;
        cursor += 2;
        if (cursor + actionLength > end) {
            return patchedCount > 0;
        }

        std::uint16_t defineCodeSize = 0;
        std::size_t defineHeaderSize = 0;
        if (actionCode == kActionDefineFunction &&
            TryReadDefineFunctionHeader(cursor, actionLength, "f_InitSaveSystem", defineCodeSize, defineHeaderSize)) {
            const std::vector<std::uint8_t> replacement = BuildDefineInitSaveSystemAction();
            out.insert(out.end(), replacement.begin(), replacement.end());
            ++patchedCount;

            const bool bodyStoredAfterAction = defineHeaderSize == actionLength && cursor + actionLength + defineCodeSize <= end;
            cursor += actionLength + (bodyStoredAfterAction ? defineCodeSize : 0);
            continue;
        }

        out.push_back(actionCode);
        out.push_back(actionLengthPtr[0]);
        out.push_back(actionLengthPtr[1]);
        out.insert(out.end(), cursor, cursor + actionLength);
        cursor += actionLength;
    }
    return patchedCount > 0;
}

void StoreU32(std::uint8_t* p, std::uint32_t value) {
    p[0] = static_cast<std::uint8_t>(value & 0xFF);
    p[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
}

bool PatchClassicSelectionFunctionBody(std::uint8_t* body, std::size_t length, int scanCount) {
    if (!body || scanCount <= 4) {
        return false;
    }

    std::uint8_t* cursor = body;
    std::uint8_t* end = body + length;
    while (cursor < end) {
        std::uint8_t* action = cursor++;
        const std::uint8_t actionCode = *action;
        if (actionCode == kActionEnd) {
            break;
        }
        if (actionCode < 0x80) {
            continue;
        }
        if (cursor + 2 > end) {
            break;
        }

        const std::uint16_t actionLength = ReadU16(cursor);
        cursor += 2;
        if (cursor + actionLength > end) {
            break;
        }

        // Both Classic selectors initialize register 6 with Push(int 4),
        // StoreRegister(6), Pop before optionally consulting the debug flag.
        if (actionCode == kActionPush && actionLength == 5 && cursor[0] == 0x07 &&
            ReadU32(cursor + 1) == 4) {
            std::uint8_t* next = cursor + actionLength;
            if (next + 5 <= end && next[0] == kActionStoreRegister &&
                ReadU16(next + 1) == 1 && next[3] == 6 && next[4] == kActionPop) {
                StoreU32(cursor + 1, static_cast<std::uint32_t>(scanCount));
                return true;
            }
        }
        cursor += actionLength;
    }
    return false;
}

bool TryReadDefineFunctionMetadata(std::uint8_t actionCode,
                                   const std::uint8_t* payload,
                                   std::uint16_t length,
                                   std::string& name,
                                   std::uint16_t& codeSize,
                                   std::size_t& headerSize) {
    name.clear();
    codeSize = 0;
    headerSize = 0;
    if (!payload || (actionCode != kActionDefineFunction && actionCode != kActionDefineFunction2)) {
        return false;
    }

    const std::uint8_t* end = payload + length;
    const auto* nameEnd = static_cast<const std::uint8_t*>(std::memchr(payload, 0, length));
    if (!nameEnd || nameEnd + 3 > end) {
        return false;
    }
    name.assign(reinterpret_cast<const char*>(payload), reinterpret_cast<const char*>(nameEnd));

    const std::uint8_t* cursor = nameEnd + 1;
    const std::uint16_t numParams = ReadU16(cursor);
    cursor += 2;
    if (actionCode == kActionDefineFunction2) {
        // registerCount (UI8) and preload/suppress flags (UI16).
        if (cursor + 3 > end) {
            return false;
        }
        cursor += 3;
    }

    for (std::uint16_t i = 0; i < numParams; ++i) {
        if (actionCode == kActionDefineFunction2) {
            if (cursor >= end) {
                return false;
            }
            ++cursor; // parameter register
        }
        const auto* paramEnd = static_cast<const std::uint8_t*>(
            std::memchr(cursor, 0, static_cast<std::size_t>(end - cursor)));
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
    headerSize = static_cast<std::size_t>(cursor - payload);
    return true;
}

void PatchClassicSelectionActions(std::uint8_t* actions,
                                  std::size_t length,
                                  int scanCount,
                                  int& patchedCount) {
    std::uint8_t* cursor = actions;
    std::uint8_t* end = actions + length;
    while (cursor < end) {
        const std::uint8_t actionCode = *cursor++;
        if (actionCode == kActionEnd) {
            return;
        }
        if (actionCode < 0x80) {
            continue;
        }
        if (cursor + 2 > end) {
            return;
        }

        const std::uint16_t actionLength = ReadU16(cursor);
        cursor += 2;
        if (cursor + actionLength > end) {
            return;
        }

        if (actionCode != kActionDefineFunction && actionCode != kActionDefineFunction2) {
            cursor += actionLength;
            continue;
        }

        std::string functionName;
        std::uint16_t codeSize = 0;
        std::size_t headerSize = 0;
        if (!TryReadDefineFunctionMetadata(
                actionCode, cursor, actionLength, functionName, codeSize, headerSize)) {
            cursor += actionLength;
            continue;
        }

        std::uint8_t* body = nullptr;
        std::uint8_t* nextAction = cursor + actionLength;
        if (headerSize == actionLength && nextAction + codeSize <= end) {
            body = nextAction;
            nextAction += codeSize;
        } else if (headerSize + codeSize <= actionLength) {
            body = cursor + headerSize;
        }

        if (body) {
            if ((functionName == "f_LobbySelectingChar" || functionName == "f_SelectingChar") &&
                PatchClassicSelectionFunctionBody(body, codeSize, scanCount)) {
                ++patchedCount;
                Core::Logger::Info("Patched Classic selector '%s' scan count 4 -> %d",
                                   functionName.c_str(),
                                   scanCount);
            }
            PatchClassicSelectionActions(body, codeSize, scanCount, patchedCount);
        }
        cursor = nextAction;
    }
}

void PatchClassicSelectionTags(std::uint8_t* tags,
                               std::uint8_t* end,
                               int scanCount,
                               int& patchedCount) {
    std::uint8_t* cursor = tags;
    while (cursor < end) {
        Tag tag;
        if (!ReadTag(cursor, end, tag)) {
            return;
        }

        auto* payload = const_cast<std::uint8_t*>(tag.payload);
        if (tag.code == kTagDoAction) {
            PatchClassicSelectionActions(payload, tag.length, scanCount, patchedCount);
        } else if (tag.code == kTagDoInitAction && tag.length >= 2) {
            PatchClassicSelectionActions(payload + 2, tag.length - 2, scanCount, patchedCount);
        } else if (tag.code == kTagDefineSprite && tag.length >= 4) {
            PatchClassicSelectionTags(payload + 4, payload + tag.length, scanCount, patchedCount);
        }

        cursor = const_cast<std::uint8_t*>(tag.next);
        if (tag.code == kTagEnd) {
            return;
        }
    }
}

bool TryPatchLobbyClassicSelectionLimit(SWFScene* scene, const std::string& swfPath,
                                       OwnedPatchedBuffers& stagedBuffers) {
    const int scanCount = Characters::Registry::Instance().ClassicVisibleCount();
    if (!scene || !scene->swfBuffer || !PathContainsLobby(swfPath) || scanCount <= 4) {
        return false;
    }

    const SWFBufferView view = MakeSWFBufferView(scene->swfBuffer);
    if (!view.valid || view.compressed || !view.tags || view.fileLength < view.headerSize) {
        Core::Logger::Warn("Classic selector patch skipped: invalid lobby buffer path='%s'", swfPath.c_str());
        return false;
    }

    auto owned = std::make_unique<std::vector<std::uint8_t>>(view.base, view.base + view.fileLength);
    std::uint8_t* base = owned->data();
    int patchedCount = 0;
    PatchClassicSelectionTags(base + view.headerSize, base + view.fileLength, scanCount, patchedCount);
    if (patchedCount == 0) {
        Core::Logger::Warn("Classic selector patch found no target functions path='%s'", swfPath.c_str());
        return false;
    }

    scene->swfBuffer = owned->data();
    stagedBuffers.push_back(std::move(owned));
    Core::Logger::Info("Patched lobby Classic character scan functions=%d scanCount=%d",
                       patchedCount,
                       scanCount);
    return true;
}

struct SpriteFrame {
    std::vector<std::uint8_t> bytes;
};

struct LobbyPortraitInjection {
    std::vector<ShapeInjection::ImportedShape> definitions;
    std::vector<std::uint16_t> classicShapeIds;
    std::vector<std::uint16_t> freshShapeIds;
};

bool ReadSpriteFrames(const std::uint8_t* tags, const std::uint8_t* end, std::vector<SpriteFrame>& frames, std::vector<std::uint8_t>& tail) {
    const std::uint8_t* cursor = tags;
    SpriteFrame current;
    while (cursor < end) {
        Tag tag;
        if (!ReadTag(cursor, end, tag)) {
            return false;
        }

        current.bytes.insert(current.bytes.end(), tag.begin, tag.next);
        cursor = tag.next;

        if (tag.code == kTagShowFrame || tag.code == 142) {
            frames.push_back(std::move(current));
            current = {};
            continue;
        }

        if (tag.code == kTagEnd) {
            tail = std::move(current.bytes);
            return true;
        }
    }

    if (!current.bytes.empty()) {
        tail = std::move(current.bytes);
    }
    return true;
}

std::string JoinPath(const std::string& base, const char* child) {
    if (base.empty()) {
        return child ? child : "";
    }
    if (base.back() == '\\' || base.back() == '/') {
        return base + child;
    }
    return base + "\\" + child;
}

std::uint16_t ImportPortraitShape(const std::string& path,
                                  ShapeInjection::ShapeIdAllocator& allocator,
                                  LobbyPortraitInjection& injection) {
    for (const ShapeInjection::ImportedShape& existing : injection.definitions) {
        if (existing.sourcePath == path) {
            return existing.shapeId;
        }
    }

    const std::uint16_t shapeId = allocator.Allocate();
    if (!shapeId) {
        return 0;
    }
    ShapeInjection::ImportedShape shape;
    if (!ShapeInjection::LoadShapeFromSWF(path, shapeId, shape)) {
        return 0;
    }
    injection.definitions.push_back(std::move(shape));
    return shapeId;
}

LobbyPortraitInjection BuildLobbyPortraitInjection(const SWFBufferView& view) {
    LobbyPortraitInjection injection;
    ShapeInjection::ShapeIdAllocator allocator(view);

    for (const Characters::AddonCharacter& addon : Characters::Registry::Instance().Addons()) {
        if (addon.assetDirectory.empty()) {
            Core::Logger::Warn("Portrait shapes not injected for character '%s': no asset directory registered",
                               addon.name.c_str());
            continue;
        }

        if (!addon.freshOnly) {
            const std::string classicPath = JoinPath(addon.assetDirectory, "portrait_classic.swf");
            const std::uint16_t classicShapeId = ImportPortraitShape(classicPath, allocator, injection);
            if (classicShapeId) {
                injection.classicShapeIds.push_back(classicShapeId);
            }
        }

        const std::string freshPath = JoinPath(addon.assetDirectory, "portrait.swf");
        const std::uint16_t freshShapeId = ImportPortraitShape(freshPath, allocator, injection);
        if (freshShapeId) {
            injection.freshShapeIds.push_back(freshShapeId);
        }
    }
    return injection;
}

bool BuildClonedPortraitFrame(const SpriteFrame& source,
                              std::uint16_t shapeId,
                              int sourceFrameNumber,
                              SpriteFrame& out) {
    out = source;
    std::uint16_t oldCharacterId = 0;
    if (!ShapeInjection::ReplacePlaceObject2CharacterAtDepth(out.bytes, 1, shapeId, &oldCharacterId)) {
        Core::Logger::Warn("Lobby portrait frame injection failed: sourceFrame=%d has no PlaceObject2 at depth 1 shapeId=%u",
                           sourceFrameNumber,
                           shapeId);
        return false;
    }
    Core::Logger::Info("Lobby portrait frame cloned sourceFrame=%d oldCharacterId=%u newShapeId=%u",
                       sourceFrameNumber,
                       oldCharacterId,
                       shapeId);
    return true;
}

bool PatchLobbyPortraitSpritePayload(const std::uint8_t* payload,
                                     std::uint32_t length,
                                     const LobbyPortraitInjection& injection,
                                     std::vector<std::uint8_t>& out) {
    if (!payload || length < 4 ||
        (injection.classicShapeIds.empty() && injection.freshShapeIds.empty())) {
        return false;
    }

    const std::uint16_t spriteId = ReadU16(payload);
    if (spriteId != kLobbyPortraitSpriteId) {
        return false;
    }

    const std::uint8_t* cursor = payload + 4;
    const std::uint8_t* end = payload + length;
    std::vector<SpriteFrame> frames;
    std::vector<std::uint8_t> tail;
    if (!ReadSpriteFrames(cursor, end, frames, tail)) {
        Core::Logger::Warn("Lobby portrait sprite frame parse failed sprite=%u", spriteId);
        return false;
    }

    const std::size_t originalFrameCount = frames.size();
    const std::size_t classicSourceIndex = static_cast<std::size_t>(kClassicCloneSourceFrame - 1);
    const std::size_t classicFirstTargetIndex = classicSourceIndex + 1;
    if (!injection.classicShapeIds.empty()) {
        const std::size_t removeTargetIndex = classicFirstTargetIndex + injection.classicShapeIds.size();
        if (classicSourceIndex >= frames.size() || removeTargetIndex >= frames.size()) {
            Core::Logger::Warn("Lobby classic portrait injection failed: frameCount=%u sourceFrame=%d portraits=%u",
                               static_cast<unsigned int>(frames.size()),
                               kClassicCloneSourceFrame,
                               static_cast<unsigned int>(injection.classicShapeIds.size()));
            return false;
        }

        const SpriteFrame removeFrame = frames[classicFirstTargetIndex];
        std::vector<SpriteFrame> classicFrames;
        classicFrames.reserve(injection.classicShapeIds.size());
        for (std::uint16_t shapeId : injection.classicShapeIds) {
            SpriteFrame cloned;
            if (!BuildClonedPortraitFrame(frames[classicSourceIndex], shapeId, kClassicCloneSourceFrame, cloned)) {
                return false;
            }
            classicFrames.push_back(std::move(cloned));
        }
        for (std::size_t i = 0; i < classicFrames.size(); ++i) {
            frames[classicFirstTargetIndex + i] = std::move(classicFrames[i]);
        }
        frames[removeTargetIndex] = removeFrame;
        Core::Logger::Info("Lobby classic portraits inserted after frame 34 count=%u frameCountUnchanged=%u",
                           static_cast<unsigned int>(injection.classicShapeIds.size()),
                           static_cast<unsigned int>(frames.size()));
    }

    if (!injection.freshShapeIds.empty()) {
        const std::size_t freshSourceIndex = static_cast<std::size_t>(kFreshCloneSourceFrame - 1);
        if (freshSourceIndex >= frames.size()) {
            Core::Logger::Warn("Lobby fresh portrait injection failed: frameCount=%u sourceFrame=%d portraits=%u",
                               static_cast<unsigned int>(frames.size()),
                               kFreshCloneSourceFrame,
                               static_cast<unsigned int>(injection.freshShapeIds.size()));
            return false;
        }

        std::vector<SpriteFrame> freshFrames;
        freshFrames.reserve(injection.freshShapeIds.size());
        for (std::uint16_t shapeId : injection.freshShapeIds) {
            SpriteFrame cloned;
            if (!BuildClonedPortraitFrame(frames[freshSourceIndex], shapeId, kFreshCloneSourceFrame, cloned)) {
                return false;
            }
            freshFrames.push_back(std::move(cloned));
        }
        frames.insert(frames.begin() + static_cast<std::ptrdiff_t>(freshSourceIndex + 1),
                      std::make_move_iterator(freshFrames.begin()),
                      std::make_move_iterator(freshFrames.end()));
        Core::Logger::Info("Lobby fresh portraits inserted after frame 135 count=%u shiftedFrames=%u",
                           static_cast<unsigned int>(injection.freshShapeIds.size()),
                           static_cast<unsigned int>(frames.size() - originalFrameCount));
    }

    out.clear();
    out.reserve(length + injection.freshShapeIds.size() * 512);
    WriteU16(out, spriteId);
    WriteU16(out, static_cast<std::uint16_t>(std::min<std::size_t>(frames.size(), 65535)));
    for (const SpriteFrame& frame : frames) {
        out.insert(out.end(), frame.bytes.begin(), frame.bytes.end());
    }
    out.insert(out.end(), tail.begin(), tail.end());

    Core::Logger::Info("Patched lobby portrait sprite=%u frames old=%u new=%u classicShapes=%u freshShapes=%u",
                       spriteId,
                       static_cast<unsigned int>(originalFrameCount),
                       static_cast<unsigned int>(frames.size()),
                       static_cast<unsigned int>(injection.classicShapeIds.size()),
                       static_cast<unsigned int>(injection.freshShapeIds.size()));
    return true;
}

bool TryPatchLobbyPortraitFrames(SWFScene* scene, const std::string& swfPath,
                                OwnedPatchedBuffers& stagedBuffers) {
    const auto& registry = Characters::Registry::Instance();
    if (!scene || !scene->swfBuffer || !PathContainsLobby(swfPath) || registry.AddonCount() <= 0) {
        return false;
    }

    Core::Logger::Info("Trying lobby portrait shape injection path='%s' addons=%d",
                       swfPath.c_str(),
                       registry.AddonCount());

    SWFBufferView view = MakeSWFBufferView(scene->swfBuffer);
    if (!view.valid || view.compressed || !view.tags || view.fileLength < view.headerSize) {
        Core::Logger::Warn("Lobby portrait patch skipped: invalid/compressed buffer path='%s' valid=%d compressed=%d fileLength=%u",
                           swfPath.c_str(),
                           view.valid ? 1 : 0,
                           view.compressed ? 1 : 0,
                           view.fileLength);
        return false;
    }

    const LobbyPortraitInjection injection = BuildLobbyPortraitInjection(view);
    if (injection.classicShapeIds.empty() && injection.freshShapeIds.empty()) {
        Core::Logger::Warn("Lobby portrait patch skipped: no valid portrait SWF shapes were loaded");
        return false;
    }

    std::vector<std::uint8_t> patched;
    patched.reserve(view.fileLength + injection.definitions.size() * 4096 + injection.freshShapeIds.size() * 512);
    patched.insert(patched.end(), view.base, view.tags);

    bool patchedSprite = false;
    bool appendedDefinitions = false;
    const std::uint8_t* cursor = view.tags;
    const std::uint8_t* end = view.base + view.fileLength;
    while (cursor < end) {
        Tag tag;
        if (!ReadTag(cursor, end, tag)) {
            Core::Logger::Error("SWF tag parse failed while patching lobby path='%s'", swfPath.c_str());
            return false;
        }

        if (tag.code == kTagEnd) {
            for (const ShapeInjection::ImportedShape& shape : injection.definitions) {
                ShapeInjection::AppendShapeDefinition(patched, shape);
            }
            AppendTag(patched, kTagEnd, std::vector<std::uint8_t>{});
            appendedDefinitions = true;
            cursor = tag.next;
            if (cursor < end) {
                patched.insert(patched.end(), cursor, end);
            }
            break;
        }

        if (!patchedSprite && tag.code == kTagDefineSprite && tag.length >= 4 && ReadU16(tag.payload) == kLobbyPortraitSpriteId) {
            std::vector<std::uint8_t> spritePayload;
            if (PatchLobbyPortraitSpritePayload(tag.payload, tag.length, injection, spritePayload)) {
                AppendTag(patched, tag.code, spritePayload);
                patchedSprite = true;
            } else {
                patched.insert(patched.end(), tag.begin, tag.next);
            }
        } else {
            patched.insert(patched.end(), tag.begin, tag.next);
        }

        cursor = tag.next;
    }

    if (!patchedSprite || !appendedDefinitions) {
        Core::Logger::Warn("Lobby portrait patch incomplete sprite=%u patchedSprite=%d appendedDefinitions=%d path='%s'",
                           kLobbyPortraitSpriteId,
                           patchedSprite ? 1 : 0,
                           appendedDefinitions ? 1 : 0,
                           swfPath.c_str());
        return false;
    }

    const std::uint32_t newLength = static_cast<std::uint32_t>(patched.size());
    patched[4] = static_cast<std::uint8_t>(newLength & 0xFF);
    patched[5] = static_cast<std::uint8_t>((newLength >> 8) & 0xFF);
    patched[6] = static_cast<std::uint8_t>((newLength >> 16) & 0xFF);
    patched[7] = static_cast<std::uint8_t>((newLength >> 24) & 0xFF);

    auto owned = std::make_unique<std::vector<std::uint8_t>>(std::move(patched));
    scene->swfBuffer = owned->data();
    stagedBuffers.push_back(std::move(owned));
    Core::Logger::Info("Patched lobby portrait shapes path='%s' definitions=%u classic=%u fresh=%u newLength=%u",
                       swfPath.c_str(),
                       static_cast<unsigned int>(injection.definitions.size()),
                       static_cast<unsigned int>(injection.classicShapeIds.size()),
                       static_cast<unsigned int>(injection.freshShapeIds.size()),
                       newLength);
    return true;
}

bool TryPatchMainSaveSystem(SWFScene* scene, const std::string& swfPath,
                            OwnedPatchedBuffers& stagedBuffers) {
    if (!scene || !scene->swfBuffer || !PathContainsMain(swfPath)) {
        return false;
    }

    Core::Logger::Info("Trying f_InitSaveSystem SWF patch for path='%s' scene=%p", swfPath.c_str(), scene);

    SWFBufferView view = MakeSWFBufferView(scene->swfBuffer);
    if (!view.valid || view.compressed || !view.tags || view.fileLength < view.headerSize) {
        Core::Logger::Warn("SWF patch skipped: invalid/compressed buffer path='%s' valid=%d compressed=%d fileLength=%u",
                           swfPath.c_str(),
                           view.valid ? 1 : 0,
                           view.compressed ? 1 : 0,
                           view.fileLength);
        return false;
    }

    std::vector<std::uint8_t> patched;
    patched.reserve(view.fileLength + 512);
    patched.insert(patched.end(), view.base, view.tags);

    int patchedActions = 0;
    const std::uint8_t* cursor = view.tags;
    const std::uint8_t* end = view.base + view.fileLength;
    while (cursor < end) {
        Tag tag;
        if (!ReadTag(cursor, end, tag)) {
            Core::Logger::Error("SWF tag parse failed while patching path='%s'", swfPath.c_str());
            return false;
        }

        if (tag.code == kTagEnd) {
            AppendTag(patched, kTagEnd, std::vector<std::uint8_t>{});
            break;
        }

        if (tag.code == kTagDoAction) {
            std::vector<std::uint8_t> actionPayload;
            PatchInitSaveSystemActions(tag.payload, tag.length, actionPayload, patchedActions);
            if (!actionPayload.empty()) {
                AppendTag(patched, tag.code, actionPayload);
            } else {
                patched.insert(patched.end(), tag.begin, tag.next);
            }
        } else if (tag.code == kTagDoInitAction && tag.length >= 2) {
            std::vector<std::uint8_t> actionPayload;
            actionPayload.push_back(tag.payload[0]);
            actionPayload.push_back(tag.payload[1]);
            PatchInitSaveSystemActions(tag.payload + 2, tag.length - 2, actionPayload, patchedActions);
            AppendTag(patched, tag.code, actionPayload);
        } else {
            patched.insert(patched.end(), tag.begin, tag.next);
        }
        cursor = tag.next;
    }

    if (patchedActions <= 0) {
        Core::Logger::Warn("f_InitSaveSystem not found in path='%s'", swfPath.c_str());
        return false;
    }

    const std::uint32_t newLength = static_cast<std::uint32_t>(patched.size());
    patched[4] = static_cast<std::uint8_t>(newLength & 0xFF);
    patched[5] = static_cast<std::uint8_t>((newLength >> 8) & 0xFF);
    patched[6] = static_cast<std::uint8_t>((newLength >> 16) & 0xFF);
    patched[7] = static_cast<std::uint8_t>((newLength >> 24) & 0xFF);

    auto owned = std::make_unique<std::vector<std::uint8_t>>(std::move(patched));
    scene->swfBuffer = owned->data();
    stagedBuffers.push_back(std::move(owned));
    Core::Logger::Info("Patched f_InitSaveSystem path='%s' patchedActions=%d newLength=%u",
                       swfPath.c_str(),
                       patchedActions,
                       newLength);
    return true;
}

char* __fastcall DetouredSWFNodeCtorLoadCOK6(char* thisPtr, void*, int a2, int pathObject, char flags) {
    const std::string previousPath = g_CurrentCok6Path;
    g_CurrentCok6Path = BestStringGuess(reinterpret_cast<int*>(pathObject));
    if (!g_CurrentCok6Path.empty()) {
        Core::Logger::Info("Loading COK6 path guess='%s'", g_CurrentCok6Path.c_str());
    }
    char* result = g_OriginalSWFNodeCtorLoadCOK6(thisPtr, a2, pathObject, flags);
    g_CurrentCok6Path = previousPath;
    return result;
}

int __fastcall DetouredSWFSceneParseHeader(int scene, void*) {
    auto* swfScene = reinterpret_cast<SWFScene*>(scene);
    OwnedPatchedBuffers stagedBuffers;
    DumpLobbyRuntimeCok6(swfScene, g_CurrentCok6Path, "before_patch");
    const bool characterPatches = g_CharacterPatchesEnabled.load();
    const bool patchedSave = characterPatches && TryPatchMainSaveSystem(swfScene, g_CurrentCok6Path, stagedBuffers);
    const bool patchedClassicSelection = characterPatches && TryPatchLobbyClassicSelectionLimit(swfScene, g_CurrentCok6Path, stagedBuffers);
    const bool patchedLobby = characterPatches && TryPatchLobbyPortraitFrames(swfScene, g_CurrentCok6Path, stagedBuffers);
    if (patchedClassicSelection || patchedLobby) {
        DumpLobbyRuntimeCok6(swfScene, g_CurrentCok6Path, "after_patch");
    }
    const bool patched = patchedSave || patchedClassicSelection || patchedLobby;
    if (patched) {
        Core::Logger::Info("SWFSceneParseHeader patched current COK6 path='%s'", g_CurrentCok6Path.c_str());
    }
    if (!stagedBuffers.empty()) {
        // The original parser has not seen these copies yet. Only the last
        // successful patch is published to it; retain that final buffer for
        // the existing lifetime, and discard superseded copies before parsing.
        auto finalBuffer = std::move(stagedBuffers.back());
        stagedBuffers.clear();
        std::lock_guard<std::mutex> lock(g_OwnedPatchedBuffersMutex);
        g_OwnedPatchedBuffers.push_back(std::move(finalBuffer));
    }
    std::string menuPath = g_CurrentCok6Path;
    std::transform(menuPath.begin(),menuPath.end(),menuPath.begin(),[](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (menuPath.find("menu") != std::string::npos && menuPath.find("amenu") == std::string::npos &&
        menuPath.find("vignettes") == std::string::npos) {
        OnlineMenuAssets::PatchScene(swfScene);
        MenuGraphicAssets::PatchScene(swfScene);
    }
    return g_OriginalSWFSceneParseHeader(scene);
}

} // namespace

bool Cok6LoadHook::Install(bool enableCharacterPatches) {
    if (g_OriginalSWFSceneParseHeader || g_OriginalSWFNodeCtorLoadCOK6) {
        if (enableCharacterPatches) g_CharacterPatchesEnabled.store(true);
        Core::Logger::Info("Cok6LoadHook already installed");
        return true;
    }

    g_OriginalSWFSceneParseHeader =
        reinterpret_cast<SWFSceneParseHeaderFn>(NativeDB::NativeAddress(NativeDB::Natives::CC_SWFSceneParseHeader));
    g_OriginalSWFNodeCtorLoadCOK6 =
        reinterpret_cast<SWFNodeCtorLoadCOK6Fn>(NativeDB::NativeAddress(NativeDB::Natives::CC_SWFNodeCtorLoadCOK6));
    Core::Logger::Info("Installing Cok6LoadHook sceneParse=%p nodeCtor=%p",
                       reinterpret_cast<void*>(g_OriginalSWFSceneParseHeader),
                       reinterpret_cast<void*>(g_OriginalSWFNodeCtorLoadCOK6));

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    LONG result = DetourAttach(reinterpret_cast<PVOID*>(&g_OriginalSWFNodeCtorLoadCOK6), DetouredSWFNodeCtorLoadCOK6);
    if (result == NO_ERROR) {
        result = DetourAttach(reinterpret_cast<PVOID*>(&g_OriginalSWFSceneParseHeader), DetouredSWFSceneParseHeader);
    }
    if (result != NO_ERROR) {
        Core::Logger::Error("Cok6LoadHook DetourAttach failed result=%ld", result);
        DetourTransactionAbort();
        g_OriginalSWFSceneParseHeader = nullptr;
        g_OriginalSWFNodeCtorLoadCOK6 = nullptr;
        return false;
    }
    const LONG commit = DetourTransactionCommit();
    Core::Logger::Info("Cok6LoadHook commit result=%ld", commit);
    if (commit == NO_ERROR) {
        if (enableCharacterPatches) g_CharacterPatchesEnabled.store(true);
    } else {
        g_OriginalSWFSceneParseHeader = nullptr;
        g_OriginalSWFNodeCtorLoadCOK6 = nullptr;
    }
    return commit == NO_ERROR;
}

void Cok6LoadHook::Remove() {
    g_CharacterPatchesEnabled.store(false);
    if (!g_OriginalSWFSceneParseHeader && !g_OriginalSWFNodeCtorLoadCOK6) {
        return;
    }
    Core::Logger::Info("Removing Cok6LoadHook");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    if (g_OriginalSWFNodeCtorLoadCOK6) {
        DetourDetach(reinterpret_cast<PVOID*>(&g_OriginalSWFNodeCtorLoadCOK6), DetouredSWFNodeCtorLoadCOK6);
    }
    if (g_OriginalSWFSceneParseHeader) {
        DetourDetach(reinterpret_cast<PVOID*>(&g_OriginalSWFSceneParseHeader), DetouredSWFSceneParseHeader);
    }
    DetourTransactionCommit();
    g_OriginalSWFSceneParseHeader = nullptr;
    g_OriginalSWFNodeCtorLoadCOK6 = nullptr;
    g_CurrentCok6Path.clear();
    Core::Logger::Info("Cok6LoadHook removed");
}

} // namespace HookCrashers::SWF
