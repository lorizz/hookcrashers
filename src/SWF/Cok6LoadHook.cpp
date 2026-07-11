#include "Cok6LoadHook.h"

#include "InitSaveSystemBytecode.h"
#include "SWFStructures.h"
#include "../Characters/CharacterRegistry.h"
#include "../Core/Logger.h"
#include "../NativeDB/Natives.h"
#include <windows.h>
#include <detours.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace HookCrashers::SWF {
namespace {

constexpr std::uint16_t kTagEnd = 0;
constexpr std::uint16_t kTagDoAction = 12;
constexpr std::uint16_t kTagDoInitAction = 59;
constexpr std::uint16_t kTagShowFrame = 1;
constexpr std::uint16_t kTagDefineSprite = 39;
constexpr std::uint8_t kActionEnd = 0x00;
constexpr std::uint8_t kActionDefineFunction = 0x9B;
constexpr std::uint16_t kLobbyPortraitSpriteId = 180;
constexpr int kClassicCloneSourceFrame = 34;
constexpr int kFreshCloneSourceFrame = 135;

using SWFSceneParseHeaderFn = int(__thiscall*)(int scene);
using SWFNodeCtorLoadCOK6Fn = char*(__thiscall*)(char* thisPtr, int a2, int pathObject, char flags);

SWFSceneParseHeaderFn g_OriginalSWFSceneParseHeader = nullptr;
SWFNodeCtorLoadCOK6Fn g_OriginalSWFNodeCtorLoadCOK6 = nullptr;
thread_local std::string g_CurrentCok6Path;
std::vector<std::unique_ptr<std::vector<std::uint8_t>>> g_OwnedPatchedBuffers;

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

struct SpriteFrame {
    std::vector<std::uint8_t> bytes;
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

        if (tag.code == kTagShowFrame) {
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

void AppendFrameClones(std::vector<SpriteFrame>& frames, int sourceFrameNumber, int cloneCount) {
    if (cloneCount <= 0) {
        return;
    }

    const int sourceIndex = sourceFrameNumber - 1;
    if (sourceIndex < 0 || sourceIndex >= static_cast<int>(frames.size())) {
        Core::Logger::Warn("Lobby portrait frame clone skipped: sourceFrame=%d cloneCount=%d frameCount=%u",
                           sourceFrameNumber,
                           cloneCount,
                           static_cast<unsigned int>(frames.size()));
        return;
    }

    const SpriteFrame templateFrame = frames[static_cast<std::size_t>(sourceIndex)];
    auto insertAt = frames.begin() + sourceIndex + 1;
    for (int i = 0; i < cloneCount; ++i) {
        insertAt = frames.insert(insertAt, templateFrame);
        ++insertAt;
    }
    Core::Logger::Info("Lobby portrait sprite cloned frame %d count=%d", sourceFrameNumber, cloneCount);
}

bool PatchLobbyPortraitSpritePayload(const std::uint8_t* payload,
                                     std::uint32_t length,
                                     int classicCloneCount,
                                     int freshCloneCount,
                                     std::vector<std::uint8_t>& out) {
    if (!payload || length < 4 || classicCloneCount + freshCloneCount <= 0) {
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

    const int originalFrameCount = static_cast<int>(frames.size());
    AppendFrameClones(frames, kClassicCloneSourceFrame, classicCloneCount);
    const int shiftedFreshSourceFrame = kFreshCloneSourceFrame + classicCloneCount;
    AppendFrameClones(frames, shiftedFreshSourceFrame, freshCloneCount);

    const int newFrameCount = static_cast<int>(frames.size());
    if (newFrameCount == originalFrameCount) {
        Core::Logger::Warn("Lobby portrait sprite no frames inserted classic=%d fresh=%d originalFrames=%d",
                           classicCloneCount,
                           freshCloneCount,
                           originalFrameCount);
        return false;
    }

    out.clear();
    out.reserve(length + static_cast<std::size_t>(newFrameCount - originalFrameCount) * 512);
    WriteU16(out, spriteId);
    const int clampedFrameCount = newFrameCount > 65535 ? 65535 : newFrameCount;
    WriteU16(out, static_cast<std::uint16_t>(clampedFrameCount));
    for (const SpriteFrame& frame : frames) {
        out.insert(out.end(), frame.bytes.begin(), frame.bytes.end());
    }
    out.insert(out.end(), tail.begin(), tail.end());

    Core::Logger::Info("Patched lobby portrait sprite=%u frames old=%d new=%d classicClones=%d freshClones=%d",
                       spriteId,
                       originalFrameCount,
                       newFrameCount,
                       classicCloneCount,
                       freshCloneCount);
    return true;
}

bool TryPatchLobbyPortraitFrames(SWFScene* scene, const std::string& swfPath) {
    const auto& registry = Characters::Registry::Instance();
    const int classicCloneCount = registry.ClassicAddonCount();
    const int freshCloneCount = registry.AddonCount();
    if (!scene || !scene->swfBuffer || !PathContainsLobby(swfPath) || classicCloneCount + freshCloneCount <= 0) {
        return false;
    }

    Core::Logger::Info("Trying lobby portrait frame patch path='%s' classicClones=%d freshClones=%d",
                       swfPath.c_str(),
                       classicCloneCount,
                       freshCloneCount);

    SWFBufferView view = MakeSWFBufferView(scene->swfBuffer);
    if (!view.valid || view.compressed || !view.tags || view.fileLength < view.headerSize) {
        Core::Logger::Warn("Lobby portrait patch skipped: invalid/compressed buffer path='%s' valid=%d compressed=%d fileLength=%u",
                           swfPath.c_str(),
                           view.valid ? 1 : 0,
                           view.compressed ? 1 : 0,
                           view.fileLength);
        return false;
    }

    std::vector<std::uint8_t> patched;
    patched.reserve(view.fileLength + 2048);
    patched.insert(patched.end(), view.base, view.tags);

    bool patchedSprite = false;
    const std::uint8_t* cursor = view.tags;
    const std::uint8_t* end = view.base + view.fileLength;
    while (cursor < end) {
        Tag tag;
        if (!ReadTag(cursor, end, tag)) {
            Core::Logger::Error("SWF tag parse failed while patching lobby path='%s'", swfPath.c_str());
            return false;
        }

        if (!patchedSprite && tag.code == kTagDefineSprite && tag.length >= 4 && ReadU16(tag.payload) == kLobbyPortraitSpriteId) {
            std::vector<std::uint8_t> spritePayload;
            if (PatchLobbyPortraitSpritePayload(tag.payload, tag.length, classicCloneCount, freshCloneCount, spritePayload)) {
                AppendTag(patched, tag.code, spritePayload);
                patchedSprite = true;
            } else {
                patched.insert(patched.end(), tag.begin, tag.next);
            }
        } else {
            patched.insert(patched.end(), tag.begin, tag.next);
        }

        cursor = tag.next;
        if (tag.code == kTagEnd) {
            break;
        }
    }

    if (!patchedSprite) {
        Core::Logger::Warn("Lobby portrait sprite %u not patched path='%s'", kLobbyPortraitSpriteId, swfPath.c_str());
        return false;
    }

    const std::uint32_t newLength = static_cast<std::uint32_t>(patched.size());
    patched[4] = static_cast<std::uint8_t>(newLength & 0xFF);
    patched[5] = static_cast<std::uint8_t>((newLength >> 8) & 0xFF);
    patched[6] = static_cast<std::uint8_t>((newLength >> 16) & 0xFF);
    patched[7] = static_cast<std::uint8_t>((newLength >> 24) & 0xFF);

    auto owned = std::make_unique<std::vector<std::uint8_t>>(std::move(patched));
    scene->swfBuffer = owned->data();
    g_OwnedPatchedBuffers.push_back(std::move(owned));
    Core::Logger::Info("Patched lobby portraits path='%s' newLength=%u", swfPath.c_str(), newLength);
    return true;
}

bool TryPatchMainSaveSystem(SWFScene* scene, const std::string& swfPath) {
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
    g_OwnedPatchedBuffers.push_back(std::move(owned));
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
    const bool patchedSave = TryPatchMainSaveSystem(swfScene, g_CurrentCok6Path);
    const bool patchedLobby = TryPatchLobbyPortraitFrames(swfScene, g_CurrentCok6Path);
    const bool patched = patchedSave || patchedLobby;
    if (patched) {
        Core::Logger::Info("SWFSceneParseHeader patched current COK6 path='%s'", g_CurrentCok6Path.c_str());
    }
    return g_OriginalSWFSceneParseHeader(scene);
}

} // namespace

bool Cok6LoadHook::Install() {
    if (g_OriginalSWFSceneParseHeader || g_OriginalSWFNodeCtorLoadCOK6) {
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
    return commit == NO_ERROR;
}

void Cok6LoadHook::Remove() {
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
