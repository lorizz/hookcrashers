#include "ShapeInjection.h"

#include "../Core/Logger.h"

#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iterator>

namespace HookCrashers::SWF::ShapeInjection {
namespace {

constexpr std::size_t kMaximumSwfSize = 64u * 1024u * 1024u;

struct Tag {
    const std::uint8_t* begin = nullptr;
    const std::uint8_t* payload = nullptr;
    const std::uint8_t* next = nullptr;
    std::uint32_t length = 0;
    std::uint16_t code = 0;
};

std::uint16_t ReadU16(const std::uint8_t* value) {
    return static_cast<std::uint16_t>(value[0])
        | static_cast<std::uint16_t>(value[1] << 8);
}

std::uint32_t ReadU32(const std::uint8_t* value) {
    return static_cast<std::uint32_t>(value[0])
        | (static_cast<std::uint32_t>(value[1]) << 8)
        | (static_cast<std::uint32_t>(value[2]) << 16)
        | (static_cast<std::uint32_t>(value[3]) << 24);
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

void PatchU16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    if (offset + 2 > bytes.size()) {
        return;
    }
    bytes[offset] = static_cast<std::uint8_t>(value & 0xFF);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

bool ReadTag(const std::uint8_t* cursor, const std::uint8_t* end, Tag& tag) {
    if (!cursor || !end || cursor > end || static_cast<std::size_t>(end - cursor) < 2) {
        return false;
    }

    tag.begin = cursor;
    const std::uint16_t packed = ReadU16(cursor);
    cursor += 2;
    tag.code = packed >> 6;
    tag.length = packed & 0x3F;
    if (tag.length == 0x3F) {
        if (static_cast<std::size_t>(end - cursor) < 4) {
            return false;
        }
        tag.length = ReadU32(cursor);
        cursor += 4;
    }
    if (tag.length > static_cast<std::uint32_t>(end - cursor)) {
        return false;
    }

    tag.payload = cursor;
    tag.next = cursor + tag.length;
    return true;
}

void AppendTag(std::vector<std::uint8_t>& out,
               std::uint16_t code,
               const std::uint8_t* payload,
               std::uint32_t length) {
    WriteU16(out, static_cast<std::uint16_t>((code << 6) | (length >= 0x3F ? 0x3F : length)));
    if (length >= 0x3F) {
        WriteU32(out, length);
    }
    if (payload && length) {
        out.insert(out.end(), payload, payload + length);
    }
}

bool IsCharacterIdTag(std::uint16_t code) {
    switch (code) {
    case 2: case 6: case 7: case 10: case 11: case 14: case 20: case 21: case 22: case 32:
    case 33: case 34: case 35: case 36: case 37: case 39: case 46: case 48: case 60: case 75:
    case 83: case 84: case 87: case 90: case 91:
        return true;
    default:
        return false;
    }
}

void MarkExportedIds(const Tag& tag, std::array<bool, 65536>& usedIds) {
    if ((tag.code != 56 && tag.code != 76) || tag.length < 2) {
        return;
    }

    const std::uint8_t* cursor = tag.payload;
    const std::uint8_t* end = tag.payload + tag.length;
    const std::uint16_t count = ReadU16(cursor);
    cursor += 2;
    for (std::uint16_t i = 0; i < count && static_cast<std::size_t>(end - cursor) >= 2; ++i) {
        usedIds[ReadU16(cursor)] = true;
        cursor += 2;
        while (cursor < end && *cursor != 0) {
            ++cursor;
        }
        if (cursor < end) {
            ++cursor;
        }
    }
}

void MarkKnownCharacterIds(const SWFBufferView& view, std::array<bool, 65536>& usedIds) {
    if (!view.valid || view.compressed || !view.tags || view.fileLength < view.headerSize) {
        return;
    }

    const std::uint8_t* cursor = view.tags;
    const std::uint8_t* end = view.base + view.fileLength;
    while (cursor < end) {
        Tag tag;
        if (!ReadTag(cursor, end, tag)) {
            break;
        }
        if (IsCharacterIdTag(tag.code) && tag.length >= 2) {
            usedIds[ReadU16(tag.payload)] = true;
        } else {
            MarkExportedIds(tag, usedIds);
        }
        cursor = tag.next;
        if (tag.code == 0) {
            break;
        }
    }
}

bool FileExists(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool ReadBinaryFile(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const std::streamoff length = file.tellg();
    if (length <= 0 || length > static_cast<std::streamoff>(kMaximumSwfSize)) {
        return false;
    }
    out.resize(static_cast<std::size_t>(length));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(out.data()), length);
    return file.good() || file.eof();
}

} // namespace

ShapeIdAllocator::ShapeIdAllocator(const SWFBufferView& view) {
    MarkKnownCharacterIds(view, m_UsedIds);
    for (std::uint32_t id = 1; id < m_UsedIds.size(); ++id) {
        if (m_UsedIds[id]) {
            m_NextId = id + 1;
        }
    }
}

std::uint16_t ShapeIdAllocator::Allocate() {
    while (m_NextId < 65534 && m_UsedIds[m_NextId]) {
        ++m_NextId;
    }
    if (m_NextId >= 65534) {
        Core::Logger::Warn("Shape injection failed: no SWF character ids available");
        return 0;
    }

    const std::uint16_t allocated = static_cast<std::uint16_t>(m_NextId++);
    m_UsedIds[allocated] = true;
    return allocated;
}

bool LoadShapeFromSWF(const std::string& path, std::uint16_t shapeId, ImportedShape& out) {
    out = {};
    if (!FileExists(path)) {
        Core::Logger::Info("Portrait shape not injected: loose SWF missing, assuming packed/legacy asset '%s'", path.c_str());
        return false;
    }
    if (!shapeId) {
        Core::Logger::Warn("Portrait shape not injected: invalid target id path='%s'", path.c_str());
        return false;
    }

    std::vector<std::uint8_t> swf;
    if (!ReadBinaryFile(path, swf)) {
        Core::Logger::Warn("Portrait shape not injected: failed to read '%s'", path.c_str());
        return false;
    }
    if (swf.size() < 12 || swf[0] != 'F' || swf[1] != 'W' || swf[2] != 'S') {
        Core::Logger::Warn("Portrait shape not injected: '%s' must be an uncompressed FWS file", path.c_str());
        return false;
    }

    const std::uint32_t declaredLength = ReadU32(swf.data() + 4);
    if (declaredLength < 12 || declaredLength > swf.size()) {
        Core::Logger::Warn("Portrait shape not injected: invalid SWF length declared=%u physical=%u path='%s'",
                           declaredLength,
                           static_cast<unsigned int>(swf.size()),
                           path.c_str());
        return false;
    }

    const std::uint8_t rectBytes = ReadRectEncodedByteCount(swf.data() + sizeof(SWFHeader));
    const std::size_t headerSize = sizeof(SWFHeader) + rectBytes + 4;
    if (headerSize > declaredLength) {
        Core::Logger::Warn("Portrait shape not injected: invalid SWF header size=%u path='%s'",
                           static_cast<unsigned int>(headerSize),
                           path.c_str());
        return false;
    }

    const std::uint8_t* cursor = swf.data() + headerSize;
    const std::uint8_t* end = swf.data() + declaredLength;
    while (cursor < end) {
        Tag tag;
        if (!ReadTag(cursor, end, tag)) {
            Core::Logger::Warn("Portrait shape not injected: malformed tag stream in '%s'", path.c_str());
            return false;
        }
        if ((tag.code == 2 || tag.code == 22 || tag.code == 32 || tag.code == 83) && tag.length >= 2) {
            out.sourcePath = path;
            out.tagCode = tag.code;
            out.shapeId = shapeId;
            out.payload.assign(tag.payload, tag.next);
            PatchU16(out.payload, 0, shapeId);
            Core::Logger::Info("Imported portrait shape path='%s' tag=%u oldId=%u newId=%u bytes=%u",
                               path.c_str(),
                               tag.code,
                               ReadU16(tag.payload),
                               shapeId,
                               tag.length);
            return true;
        }
        cursor = tag.next;
        if (tag.code == 0) {
            break;
        }
    }

    Core::Logger::Warn("Portrait shape not injected: no DefineShape tag found in '%s'", path.c_str());
    return false;
}

void AppendShapeDefinition(std::vector<std::uint8_t>& out, const ImportedShape& shape) {
    if (!shape.tagCode || shape.payload.size() < 2) {
        return;
    }
    AppendTag(out,
              shape.tagCode,
              shape.payload.data(),
              static_cast<std::uint32_t>(shape.payload.size()));
}

bool ReplacePlaceObject2CharacterAtDepth(std::vector<std::uint8_t>& frame,
                                         std::uint16_t wantedDepth,
                                         std::uint16_t shapeId,
                                         std::uint16_t* oldCharacterId) {
    const std::uint8_t* base = frame.data();
    const std::uint8_t* cursor = base;
    const std::uint8_t* end = base + frame.size();
    while (cursor < end) {
        Tag tag;
        if (!ReadTag(cursor, end, tag)) {
            return false;
        }
        if (tag.code == 26 && tag.length >= 5 && (tag.payload[0] & 0x02) != 0) {
            const std::uint16_t depth = ReadU16(tag.payload + 1);
            if (depth == wantedDepth) {
                const std::size_t payloadOffset = static_cast<std::size_t>(tag.payload - base);
                if (oldCharacterId) {
                    *oldCharacterId = ReadU16(tag.payload + 3);
                }
                frame[payloadOffset] |= 0x03;
                PatchU16(frame, payloadOffset + 3, shapeId);
                return true;
            }
        }
        cursor = tag.next;
    }
    return false;
}

} // namespace HookCrashers::SWF::ShapeInjection
