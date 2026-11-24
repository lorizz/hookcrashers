#include "MenuGraphicAssets.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>

#if defined(_WIN32)
#include "SWFStructures.h"
#include "../Core/Logger.h"
#include <windows.h>
#endif

namespace HookCrashers::SWF::MenuGraphicAssets {
namespace {

constexpr std::size_t kMenuLimit = 64u * 1024 * 1024;

struct Tag {
    std::uint16_t code = 0;
    std::size_t begin = 0, payload = 0, end = 0;
};

unsigned U16(const std::uint8_t* p) { return p[0] | (unsigned(p[1]) << 8); }
std::uint32_t U32(const std::uint8_t* p) { return U16(p) | (std::uint32_t(U16(p + 2)) << 16); }
void Put16(std::vector<std::uint8_t>& b, unsigned n) { b.push_back(n & 255); b.push_back((n >> 8) & 255); }
void Set16(std::vector<std::uint8_t>& b, std::size_t p, unsigned n) { b[p] = n & 255; b[p + 1] = (n >> 8) & 255; }
void Set32(std::vector<std::uint8_t>& b, std::size_t p, std::uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) b[p + i] = static_cast<std::uint8_t>(n >> (8 * i));
}

bool ReadTag(const std::uint8_t* b, std::size_t bytes, std::size_t offset, Tag& t) {
    if (offset > bytes || bytes - offset < 2) return false;
    const auto word = U16(b + offset);
    t = {static_cast<std::uint16_t>(word >> 6), offset, offset + 2, 0};
    std::size_t length = word & 63;
    if (length == 63) {
        if (bytes - t.payload < 4) return false;
        length = U32(b + t.payload);
        t.payload += 4;
    }
    if (length > bytes - t.payload) return false;
    t.end = t.payload + length;
    return true;
}

// Offset of the first tag of an uncompressed SWF, or 0.
std::size_t TagsStart(const std::uint8_t* b, std::size_t n) {
    if (!b || n < 13 || std::memcmp(b, "FWS", 3) != 0) return 0;
    const std::size_t rectBits = 5 + 4 * (b[8] >> 3);
    const std::size_t start = 8 + (rectBits + 7) / 8 + 4;
    return start <= n ? start : 0;
}

void AppendTag(std::vector<std::uint8_t>& out, unsigned code, const std::vector<std::uint8_t>& payload) {
    Put16(out, (code << 6) | 63);
    const auto p = out.size();
    out.resize(p + 4);
    Set32(out, p, static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

// Tags whose first u16 defines a character id that HookCrashers can copy.
bool CopyableCharacter(unsigned code) {
    switch (code) {
    case 2: case 22: case 32: case 83:   // DefineShape 1-4 (vector fills only, see below)
    case 46: case 84:                    // DefineMorphShape 1-2
    case 10: case 48: case 75:           // DefineFont, DefineFont2, DefineFont3
    case 37:                             // DefineEditText (font id remapped)
    case 39:                             // DefineSprite (placements remapped)
    case 14:                             // DefineSound
    case 87:                             // DefineBinaryData
        return true;
    }
    return false;
}

// Font metadata tags that reference (not define) a font id in their first u16.
bool FontReference(unsigned code) {
    return code == 13 || code == 62 || code == 73 || code == 88;
}

// Character tags HookCrashers cannot remap safely; their presence rejects the SWF.
const char* UnsupportedCharacter(unsigned code) {
    switch (code) {
    case 6: case 8: case 20: case 21: case 35: case 36: case 90:
        return "bitmaps are not supported yet (use vector art)";
    case 7: case 34:
        return "buttons are not supported (menu items provide the interaction)";
    case 11: case 33:
        return "static text is not supported (convert text to shapes or use dynamic text)";
    case 60: case 61:
        return "video is not supported";
    case 57: case 71:
        return "ImportAssets is not supported";
    }
    return nullptr;
}

// Sprite timeline tags that may appear inside an imported DefineSprite.
bool AllowedSpriteTag(unsigned code) {
    switch (code) {
    case 0: case 1:          // End, ShowFrame
    case 4: case 26:         // PlaceObject, PlaceObject2
    case 5: case 28:         // RemoveObject, RemoveObject2
    case 12:                 // DoAction
    case 15:                 // StartSound
    case 18: case 45: case 19: // SoundStreamHead/Head2/Block
    case 43:                 // FrameLabel
        return true;
    }
    return false;
}

using Remap = std::array<std::uint16_t, 65536>;

bool RemapSprite(std::vector<std::uint8_t>& payload, const Remap& map, std::string& error) {
    // payload: SpriteID(2) FrameCount(2) tags...
    for (std::size_t start = 4;;) {
        Tag t;
        if (!ReadTag(payload.data(), payload.size(), start, t)) { error = "malformed sprite timeline"; return false; }
        start = t.end;
        if (!AllowedSpriteTag(t.code)) {
            error = t.code == 70 ? "PlaceObject3 is not supported (publish for Flash Player 7/8)"
                                 : "unsupported tag " + std::to_string(t.code) + " inside a sprite";
            return false;
        }
        const auto n = t.end - t.payload;
        std::size_t idOffset = 0;
        if ((t.code == 4 || t.code == 5 || t.code == 15) && n >= 2) idOffset = t.payload;
        if (t.code == 26 && n >= 5 && (payload[t.payload] & 2)) idOffset = t.payload + 3;
        if (idOffset) {
            const auto old = U16(payload.data() + idOffset);
            if (!map[old]) { error = "sprite places a character that is not defined in the SWF"; return false; }
            Set16(payload, idOffset, map[old]);
        }
        if (t.code == 0) {
            if (n != 0) { error = "malformed sprite End"; return false; }
            return true;
        }
    }
}

bool RemapEditTextFont(std::vector<std::uint8_t>& p, const Remap& map, std::string& error) {
    if (p.size() < 3) { error = "malformed DefineEditText"; return false; }
    const std::size_t flags = 2 + (5 + 4 * (p[2] >> 3) + 7) / 8;
    if (flags + 2 > p.size()) { error = "malformed DefineEditText"; return false; }
    if (p[flags + 1] & 0x80) { error = "DefineEditText with a font class is not supported"; return false; }
    if (p[flags] & 1) { // HasFont
        if (flags + 4 > p.size()) { error = "malformed DefineEditText"; return false; }
        const auto font = U16(p.data() + flags + 2);
        if (!map[font]) { error = "text field uses a font that is not defined in the SWF"; return false; }
        Set16(p, flags + 2, map[font]);
    }
    return true;
}

struct ExportInfo {
    bool found = false;
    std::uint16_t id = 0;
};

// Collects exports and validates every top-level tag. No allocation of ids.
bool Scan(const std::uint8_t* data, std::size_t bytes, const std::string& exportName,
          std::set<std::uint16_t>& defined, std::set<std::uint16_t>& sprites, ExportInfo& root,
          std::string& error) {
    if (!data || bytes > kMaxSwfBytes) { error = "missing or oversized SWF"; return false; }
    if (bytes >= 3 && (std::memcmp(data, "CWS", 3) == 0 || std::memcmp(data, "ZWS", 3) == 0)) {
        error = "compressed SWF (CWS/ZWS); publish it uncompressed (FWS)";
        return false;
    }
    std::size_t p = TagsStart(data, bytes);
    if (!p) { error = "not an uncompressed SWF"; return false; }
    if (U32(data + 4) != bytes) { error = "SWF FileLength does not match the file size"; return false; }
    for (Tag t {}; ; p = t.end) {
        if (!ReadTag(data, bytes, p, t)) { error = "malformed tag stream"; return false; }
        if (t.code == 0) {
            if (t.end != t.payload) { error = "malformed End tag"; return false; }
            break;
        }
        if (const char* reason = UnsupportedCharacter(t.code)) { error = reason; return false; }
        if (t.code == 69 && t.end > t.payload && (data[t.payload] & 0x08)) {
            error = "ActionScript 3 SWF; Castle Crashers menus use ActionScript 2";
            return false;
        }
        if (CopyableCharacter(t.code)) {
            if (t.end - t.payload < 2) { error = "malformed character tag"; return false; }
            const auto id = static_cast<std::uint16_t>(U16(data + t.payload));
            if (!id || !defined.insert(id).second) { error = "duplicate or zero character id"; return false; }
            if (t.code == 39) sprites.insert(id);
        }
        if (t.code == 56 && t.end - t.payload >= 2) {
            std::size_t entry = t.payload + 2;
            for (unsigned count = U16(data + t.payload); count; --count) {
                if (entry + 2 > t.end) { error = "malformed ExportAssets"; return false; }
                const auto id = static_cast<std::uint16_t>(U16(data + entry));
                entry += 2;
                const auto* zero = static_cast<const std::uint8_t*>(std::memchr(data + entry, 0, t.end - entry));
                if (!zero) { error = "malformed ExportAssets"; return false; }
                const std::string name(reinterpret_cast<const char*>(data + entry), static_cast<std::size_t>(zero - data - entry));
                if (name == exportName) root = {true, id};
                entry = static_cast<std::size_t>(zero - data) + 1;
            }
        }
    }
    if (!root.found && exportName.size() > 1 && exportName[0] == '#') {
        // "#<id>": a sprite selected by character id, for SWFs without linkage names.
        char* end = nullptr;
        const unsigned long id = std::strtoul(exportName.c_str() + 1, &end, 10);
        if (end && *end == '\0' && id > 0 && id < 65536) root = {true, static_cast<std::uint16_t>(id)};
    }
    if (!root.found) { error = "export '" + exportName + "' not found"; return false; }
    if (!sprites.count(root.id)) { error = "export '" + exportName + "' is not a sprite (MovieClip)"; return false; }
    return true;
}

// Copies the definitions of `data` into `definitions` with ids starting at
// nextId, then exports the root sprite as `alias`.
bool Import(const std::uint8_t* data, std::size_t bytes, const std::string& exportName,
            const std::string& alias, unsigned& nextId, std::vector<std::uint8_t>& definitions,
            std::string& error) {
    std::set<std::uint16_t> defined, sprites;
    ExportInfo root;
    if (!Scan(data, bytes, exportName, defined, sprites, root, error)) return false;
    if (nextId + defined.size() >= 65535) { error = "MENU has no free character ids"; return false; }

    Remap map {};
    unsigned next = nextId;
    for (std::uint16_t id : defined) map[id] = static_cast<std::uint16_t>(next++);

    std::vector<std::uint8_t> staged;
    std::size_t p = TagsStart(data, bytes);
    for (Tag t {}; ; p = t.end) {
        ReadTag(data, bytes, p, t);
        if (t.code == 0) break;
        const bool character = CopyableCharacter(t.code);
        if (!character && !FontReference(t.code)) continue;
        std::vector<std::uint8_t> payload(data + t.payload, data + t.end);
        if (payload.size() < 2) { error = "malformed tag"; return false; }
        const auto old = U16(payload.data());
        if (!map[old]) {
            error = FontReference(t.code) ? "font metadata for an undefined font" : "undefined character";
            return false;
        }
        Set16(payload, 0, map[old]);
        if (t.code == 39) {
            if (payload.size() < 4 || !RemapSprite(payload, map, error)) return false;
        } else if (t.code == 37) {
            if (!RemapEditTextFont(payload, map, error)) return false;
        }
        AppendTag(staged, t.code, payload);
    }
    std::vector<std::uint8_t> exports;
    Put16(exports, 1);
    Put16(exports, map[root.id]);
    exports.insert(exports.end(), alias.begin(), alias.end());
    exports.push_back(0);
    AppendTag(staged, 56, exports);

    definitions.insert(definitions.end(), staged.begin(), staged.end());
    nextId = next;
    return true;
}

struct MenuInfo {
    unsigned highest = 0;
    std::size_t insertion = 0;
    std::set<std::string> exports;
};

bool ScanMenu(const std::uint8_t* menu, std::size_t bytes, MenuInfo& info) {
    std::size_t p = TagsStart(menu, bytes);
    if (!p || bytes > kMenuLimit) return false;
    for (Tag t {}; p < bytes; p = t.end) {
        if (!ReadTag(menu, bytes, p, t)) return false;
        if (!t.code) {
            if (t.end != t.payload) return false;
            info.insertion = t.begin;
            return info.highest < 65000;
        }
        // Any tag that defines a character in the stock MENU (wider than the copy list).
        switch (t.code) {
        case 2: case 6: case 7: case 10: case 11: case 14: case 20: case 21: case 22: case 32:
        case 33: case 34: case 35: case 36: case 37: case 39: case 46: case 48: case 60: case 75:
        case 83: case 84: case 87: case 90: case 91:
            if (t.end - t.payload >= 2) info.highest = (std::max)(info.highest, U16(menu + t.payload));
            break;
        }
        if (t.code == 56 && t.end - t.payload >= 2) {
            std::size_t entry = t.payload + 2;
            for (unsigned count = U16(menu + t.payload); count; --count) {
                if (entry + 2 > t.end) return false;
                info.highest = (std::max)(info.highest, U16(menu + entry));
                entry += 2;
                const auto* zero = static_cast<const std::uint8_t*>(std::memchr(menu + entry, 0, t.end - entry));
                if (!zero) return false;
                info.exports.emplace(reinterpret_cast<const char*>(menu + entry), static_cast<std::size_t>(zero - menu - entry));
                entry = static_cast<std::size_t>(zero - menu) + 1;
            }
        }
    }
    return false;
}

struct Registered {
    std::string id;
    std::string exportName;
    std::string alias;
    std::vector<std::uint8_t> swf;
};

std::mutex g_lock;
std::vector<Registered> g_graphics;
std::set<std::string> g_inScene;
#if defined(_WIN32)
std::vector<std::unique_ptr<std::vector<std::uint8_t>>> g_buffers;
#endif

} // namespace

bool IsMainMenuMovie(const std::uint8_t* menu, std::size_t bytes) {
    MenuInfo info;
    return ScanMenu(menu, bytes, info) && info.exports.count("playwithfriends") != 0;
}

bool Validate(const std::uint8_t* swf, std::size_t bytes, const std::string& exportName, std::string& error) {
    std::vector<std::uint8_t> sink;
    unsigned next = 1;
    return Import(swf, bytes, exportName, "hcg_validate", next, sink, error);
}

bool Build(const std::uint8_t* menu, std::size_t bytes, const std::vector<Source>& sources,
           std::vector<std::uint8_t>& output, std::vector<std::string>& imported,
           std::vector<std::string>& failures) {
    imported.clear();
    failures.clear();
    MenuInfo info;
    if (!ScanMenu(menu, bytes, info)) return false;

    unsigned next = info.highest + 1;
    std::vector<std::uint8_t> definitions;
    for (const Source& source : sources) {
        if (info.exports.count(source.alias)) {
            imported.push_back(source.alias); // already present (scene patched before)
            continue;
        }
        std::string error;
        if (!Import(source.data, source.bytes, source.exportName, source.alias, next, definitions, error)) {
            failures.push_back(source.alias + ": " + error);
            continue;
        }
        info.exports.insert(source.alias);
        imported.push_back(source.alias);
    }
    if (definitions.empty()) return true;

    std::vector<std::uint8_t> patched(menu, menu + info.insertion);
    patched.insert(patched.end(), definitions.begin(), definitions.end());
    patched.insert(patched.end(), menu + info.insertion, menu + bytes);
    if (patched.size() > kMenuLimit) {
        imported.clear();
        failures.push_back("patched MENU exceeds the size limit");
        return true;
    }
    Set32(patched, 4, static_cast<std::uint32_t>(patched.size()));
    output = std::move(patched);
    return true;
}

RegisterResult Register(const std::string& id, std::vector<std::uint8_t> swf,
                        const std::string& exportName, std::string& error) {
    if (id.empty() || id.size() > 64 || exportName.empty()) {
        error = "graphic id and export name are required";
        return RegisterResult::InvalidArgument;
    }
    if (!Validate(swf.data(), swf.size(), exportName, error)) return RegisterResult::Unsupported;
    std::lock_guard<std::mutex> lock(g_lock);
    for (const Registered& graphic : g_graphics) {
        if (graphic.id == id) { error = "graphic id already registered"; return RegisterResult::DuplicateId; }
    }
    if (g_graphics.size() >= kMaxGraphics) { error = "too many menu graphics"; return RegisterResult::LimitReached; }
    Registered graphic;
    graphic.id = id;
    graphic.exportName = exportName;
    graphic.alias = "hcg_" + std::to_string(g_graphics.size());
    graphic.swf = std::move(swf);
    g_graphics.push_back(std::move(graphic));
    return RegisterResult::Ok;
}

bool IsRegistered(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_lock);
    return std::any_of(g_graphics.begin(), g_graphics.end(), [&](const Registered& g) { return g.id == id; });
}

std::string AliasFor(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_lock);
    for (const Registered& graphic : g_graphics) {
        if (graphic.id == id) return graphic.alias;
    }
    return {};
}

bool IsAvailableInScene(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_lock);
    for (const Registered& graphic : g_graphics) {
        if (graphic.id == id) return g_inScene.count(graphic.alias) != 0;
    }
    return false;
}

void ClearForTests() {
    std::lock_guard<std::mutex> lock(g_lock);
    g_graphics.clear();
    g_inScene.clear();
}

#if defined(_WIN32)
namespace {
// Same physical-length walk as OnlineMenuAssets: MENU's legacy FileLength is
// smaller than its tag stream, and buffers may end at an unmapped page.
std::size_t MeasurePhysical(const std::uint8_t* b) {
    __try {
        if (!b || std::memcmp(b, "FWS", 3) != 0) return 0;
        std::size_t p = 8 + (5 + 4 * (b[8] >> 3) + 7) / 8 + 4;
        for (Tag t {}; p < kMenuLimit; p = t.end) {
            if (!ReadTag(b, kMenuLimit, p, t)) return 0;
            if (t.end > t.payload) { volatile auto touched = b[t.end - 1]; (void)touched; }
            if (!t.code) return t.payload == t.end ? t.end : 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return 0;
}
} // namespace

bool PatchScene(void* rawScene) {
    auto* scene = static_cast<SWFScene*>(rawScene);
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_graphics.empty()) return true;
    if (!scene || !scene->swfBuffer) return false;
    const std::size_t bytes = MeasurePhysical(scene->swfBuffer);
    if (!bytes) {
        Core::Logger::Error("Menu graphics: MENU buffer could not be measured; no graphics imported");
        return false;
    }
    // Other movies whose path contains "menu" must not receive mod graphics.
    if (!IsMainMenuMovie(scene->swfBuffer, bytes)) return true;
    g_inScene.clear();
    std::vector<Source> sources;
    for (const Registered& graphic : g_graphics) {
        sources.push_back({graphic.swf.data(), graphic.swf.size(), graphic.exportName, graphic.alias});
    }
    auto patched = std::make_unique<std::vector<std::uint8_t>>();
    std::vector<std::string> imported, failures;
    if (!Build(scene->swfBuffer, bytes, sources, *patched, imported, failures)) {
        Core::Logger::Error("Menu graphics: MENU tag stream is not usable; no graphics imported");
        return false;
    }
    for (const std::string& failure : failures) Core::Logger::Error("Menu graphics: %s", failure.c_str());
    if (!patched->empty()) {
        scene->swfBuffer = patched->data();
        g_buffers.push_back(std::move(patched));
    }
    g_inScene.insert(imported.begin(), imported.end());
    Core::Logger::Info("Menu graphics: %u of %u imported into MENU",
                       static_cast<unsigned>(imported.size()), static_cast<unsigned>(g_graphics.size()));
    return failures.empty();
}
#else
bool PatchScene(void*) {
    return false;
}
#endif

} // namespace HookCrashers::SWF::MenuGraphicAssets
