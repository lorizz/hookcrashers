#include "SaveSlotButtonAsset.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>

#ifdef _WIN32
#include "SWFStructures.h"
#include "../Core/Logger.h"
#include <windows.h>
#endif

namespace HookCrashers::SWF::SaveSlotButtonAsset {
namespace {

constexpr std::size_t kLimit = 64u * 1024u * 1024u;
constexpr unsigned kTagEnd = 0, kTagShowFrame = 1, kTagCastleFrame = 142;
constexpr unsigned kTagPlaceObject2 = 26, kTagRemoveObject2 = 28, kTagDefineSprite = 39;

// buttonchoices (build 0x6A4D42BF) and the characters it places in the row.
constexpr unsigned kButtonChoices = 781;
constexpr unsigned kRedButtonSprite = 684, kRedButtonShape = 683;
constexpr unsigned kGreenButtonSprite = 690;
constexpr unsigned kRedIcon = 739, kWrenchIcon = 741;
constexpr unsigned kWideHighlight = 760, kNarrowHighlight = 754;
constexpr unsigned kDepthRed = 13, kDepthGreen = 19, kDepthRedIcon = 34, kDepthWrench = 36, kDepthHighlight = 27;
constexpr unsigned kDepthSlots = 20, kDepthSlotsIcon = 37;     // free depths in buttonchoices

// Offsets in twips, from the measured row: green 172.2 px wide, red 81.2 px,
// three 81.2 px buttons with 4.8 px gaps fill the same 253.2 px.
constexpr int kStepTwips = 1718;          // one button + gap (85.9 px)
constexpr int kGreenShift = -2 * kStepTwips - 2;
constexpr int kWrenchShift = -910;        // wide green centre -> new green centre
constexpr int kSlotsIconDx = -171, kSlotsIconDy = -54;

constexpr float kHueGreen = 120.0f, kHueTeal = 176.0f;

struct Tag { unsigned code; std::size_t begin, payload, end; };

unsigned U16(const std::uint8_t* p) { return p[0] | (unsigned(p[1]) << 8); }
std::uint32_t U32(const std::uint8_t* p) { return U16(p) | (std::uint32_t(U16(p + 2)) << 16); }

bool ReadTag(const std::uint8_t* b, std::size_t n, std::size_t offset, Tag& t) {
    if (offset > n || n - offset < 2) return false;
    const unsigned word = U16(b + offset);
    t = {word >> 6, offset, offset + 2, 0};
    std::size_t length = word & 63;
    if (length == 63) {
        if (n - t.payload < 4) return false;
        length = U32(b + t.payload);
        t.payload += 4;
    }
    if (length > n - t.payload) return false;
    t.end = t.payload + length;
    return true;
}

void PutTag(std::vector<std::uint8_t>& out, unsigned code, const std::vector<std::uint8_t>& payload) {
    const std::uint32_t n = static_cast<std::uint32_t>(payload.size());
    const unsigned word = (code << 6) | 63;
    out.push_back(word & 255);
    out.push_back(word >> 8);
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(n >> (8 * i)));
    out.insert(out.end(), payload.begin(), payload.end());
}

// --- Bits --------------------------------------------------------------------------
struct BitReader {
    const std::uint8_t* b;
    std::size_t bit;
    std::uint32_t U(unsigned n) {
        std::uint32_t v = 0;
        while (n--) { v = (v << 1) | ((b[bit >> 3] >> (7 - (bit & 7))) & 1); ++bit; }
        return v;
    }
    std::int32_t S(unsigned n) {
        if (!n) return 0;
        const std::uint32_t v = U(n);
        return (v >> (n - 1)) ? static_cast<std::int32_t>(v | (~0u << n)) : static_cast<std::int32_t>(v);
    }
    std::size_t Bytes() const { return (bit + 7) >> 3; }
};

struct BitWriter {
    std::vector<std::uint8_t>& out;
    unsigned used = 8;
    void U(std::uint32_t v, unsigned n) {
        while (n--) {
            if (used == 8) { out.push_back(0); used = 0; }
            if ((v >> n) & 1) out.back() |= static_cast<std::uint8_t>(0x80 >> used);
            ++used;
        }
    }
    void S(std::int32_t v, unsigned n) { U(static_cast<std::uint32_t>(v) & ((n >= 32) ? ~0u : ((1u << n) - 1)), n); }
    void Flush() { used = 8; }
};

unsigned SignedBits(std::int32_t v) {
    unsigned n = 1;
    while (n < 31 && (v < -(1 << (n - 1)) || v > (1 << (n - 1)) - 1)) ++n;
    return n;
}

void PutMatrix(std::vector<std::uint8_t>& out, std::int32_t tx, std::int32_t ty) {
    BitWriter w {out};
    w.U(0, 1); // no scale
    w.U(0, 1); // no rotate
    const unsigned n = (std::max)(SignedBits(tx), SignedBits(ty));
    w.U(n, 5);
    w.S(tx, n);
    w.S(ty, n);
}

std::size_t SkipRect(const std::uint8_t* b, std::size_t p) {
    return p + (5 + 4 * (b[p] >> 3) + 7) / 8;
}

// PlaceObject2 that places a character with a translate-only matrix.
struct Placement {
    unsigned flags = 0, depth = 0, character = 0;
    std::int32_t tx = 0, ty = 0;
    bool hasRatio = false;
    unsigned ratio = 0;
    bool simple = false; // HasCharacter|HasMatrix (+HasRatio), translate only
};

Placement ReadPlacement(const std::uint8_t* b, const Tag& t) {
    Placement p;
    if (t.end - t.payload < 3) return p;
    p.flags = b[t.payload];
    p.depth = U16(b + t.payload + 1);
    std::size_t q = t.payload + 3;
    if (p.flags & 2) { p.character = U16(b + q); q += 2; }
    if ((p.flags & ~0x10u) != 0x06) return p;
    BitReader r {b, q * 8};
    if (r.U(1) || r.U(1)) return p; // scale/rotate present
    const unsigned n = r.U(5);
    p.tx = r.S(n);
    p.ty = r.S(n);
    q = r.Bytes();
    if (p.flags & 0x10) {
        if (q + 2 > t.end) return p;
        p.hasRatio = true;
        p.ratio = U16(b + q);
        q += 2;
    }
    p.simple = q == t.end;
    return p;
}

std::vector<std::uint8_t> PlacementPayload(unsigned depth, unsigned character, std::int32_t tx, std::int32_t ty,
                                           int ratio = -1) {
    std::vector<std::uint8_t> p {static_cast<std::uint8_t>(ratio >= 0 ? 0x16 : 0x06), static_cast<std::uint8_t>(depth & 255), static_cast<std::uint8_t>(depth >> 8),
                                 static_cast<std::uint8_t>(character & 255), static_cast<std::uint8_t>(character >> 8)};
    PutMatrix(p, tx, ty);
    if (ratio >= 0) {
        p.push_back(static_cast<std::uint8_t>(ratio & 255));
        p.push_back(static_cast<std::uint8_t>(ratio >> 8));
    }
    return p;
}

// --- Colors ------------------------------------------------------------------------
void Recolor(std::uint8_t* rgb, float hue) {
    const float r = rgb[0] / 255.0f, g = rgb[1] / 255.0f, b = rgb[2] / 255.0f;
    const float mx = (std::max)({r, g, b}), mn = (std::min)({r, g, b});
    const float delta = mx - mn;
    if (mx <= 0.0f || delta / mx < 0.12f) return; // white, greys: unchanged
    const float s = delta / mx, v = mx;
    const float h = hue / 60.0f;
    const int sector = static_cast<int>(std::floor(h)) % 6;
    const float f = h - std::floor(h);
    const float p = v * (1 - s), qq = v * (1 - s * f), tt = v * (1 - s * (1 - f));
    float out[3];
    switch (sector) {
    case 0: out[0] = v; out[1] = tt; out[2] = p; break;
    case 1: out[0] = qq; out[1] = v; out[2] = p; break;
    case 2: out[0] = p; out[1] = v; out[2] = tt; break;
    case 3: out[0] = p; out[1] = qq; out[2] = v; break;
    case 4: out[0] = tt; out[1] = p; out[2] = v; break;
    default: out[0] = v; out[1] = p; out[2] = qq; break;
    }
    for (int i = 0; i < 3; ++i) rgb[i] = static_cast<std::uint8_t>(std::lround(out[i] * 255.0f));
}

// The original wide green button uses these greens; map the red button's
// colors onto them so the new green matches the wrench icon's background.
bool GreenPalette(std::uint8_t* rgb) {
    struct Map { std::uint8_t from[3], to[3]; };
    static const Map kMap[] = {
        {{79, 52, 52}, {52, 79, 52}},   {{201, 55, 55}, {51, 170, 51}}, {{238, 94, 94}, {102, 204, 102}},
        {{240, 97, 97}, {102, 204, 102}}, {{226, 74, 74}, {84, 187, 84}},
    };
    for (const Map& m : kMap) {
        if (rgb[0] == m.from[0] && rgb[1] == m.from[1] && rgb[2] == m.from[2]) {
            std::memcpy(rgb, m.to, 3);
            return true;
        }
    }
    return false;
}

void RecolorStyle(std::uint8_t* rgb, float hue) {
    if (hue == kHueGreen && GreenPalette(rgb)) return;
    Recolor(rgb, hue);
}

// DefineShape (code 2) copy with a new id and every style color moved to `hue`.
bool RecoloredShape(const std::uint8_t* b, const Tag& t, unsigned newId, float hue, std::vector<std::uint8_t>& payload) {
    if (t.code != 2) return false;
    payload.assign(b + t.payload, b + t.end);
    payload[0] = newId & 255;
    payload[1] = newId >> 8;
    std::uint8_t* d = payload.data();
    const std::size_t n = payload.size();
    std::size_t q = SkipRect(d, 2);
    auto count = [&](std::size_t& at) -> unsigned {
        if (at >= n) return 0;
        unsigned c = d[at++];
        if (c == 0xFF) { c = U16(d + at); at += 2; }
        return c;
    };
    const unsigned fills = count(q);
    for (unsigned i = 0; i < fills; ++i) {
        if (q >= n) return false;
        const unsigned type = d[q++];
        if (type == 0x00) {
            RecolorStyle(d + q, hue);
            q += 3;
        } else if (type == 0x10 || type == 0x12) {
            BitReader r {d, q * 8};
            if (r.U(1)) { const unsigned k = r.U(5); r.U(k); r.U(k); }
            if (r.U(1)) { const unsigned k = r.U(5); r.U(k); r.U(k); }
            const unsigned k = r.U(5); r.U(k); r.U(k);
            q = r.Bytes();
            const unsigned stops = d[q++] & 15;
            for (unsigned s = 0; s < stops; ++s) { ++q; RecolorStyle(d + q, hue); q += 3; }
        } else if (type >= 0x40 && type <= 0x43) {
            q += 2;
            BitReader r {d, q * 8};
            if (r.U(1)) { const unsigned k = r.U(5); r.U(k); r.U(k); }
            if (r.U(1)) { const unsigned k = r.U(5); r.U(k); r.U(k); }
            const unsigned k = r.U(5); r.U(k); r.U(k);
            q = r.Bytes();
        } else {
            return false;
        }
    }
    const unsigned lines = count(q);
    for (unsigned i = 0; i < lines; ++i) {
        if (q + 5 > n) return false;
        RecolorStyle(d + q + 2, hue);
        q += 5;
    }
    return q < n;
}

// White floppy disk, 26x26 px centred on 0,0: cut top-right corner, a shutter
// window at the top and a label window at the bottom.
std::vector<std::uint8_t> SlotIconShape(unsigned id) {
    std::vector<std::uint8_t> p {static_cast<std::uint8_t>(id & 255), static_cast<std::uint8_t>(id >> 8)};
    constexpr int S = 20; // px -> twips
    {
        BitWriter w {p};
        const unsigned nb = SignedBits(14 * S);
        w.U(nb, 5);
        w.S(-14 * S, nb); w.S(14 * S, nb); w.S(-14 * S, nb); w.S(14 * S, nb);
    }
    p.push_back(1);                 // one fill style
    p.push_back(0x00);
    p.push_back(255); p.push_back(255); p.push_back(255);
    p.push_back(0);                 // no line styles
    p.push_back(0x10);              // NumFillBits=1, NumLineBits=0

    BitWriter w {p};
    auto path = [&](const std::vector<std::pair<int, int>>& pts, bool hole) {
        // Style change: move to the first point, fill on the right (outline) or the left (hole).
        w.U(0, 1);
        w.U(0, 1); w.U(0, 1);       // new styles, line style
        w.U(1, 1);                  // fill style 1
        w.U(1, 1);                  // fill style 0
        w.U(1, 1);                  // move to
        const int mx = pts[0].first * S, my = pts[0].second * S;
        const unsigned mb = (std::max)(SignedBits(mx), SignedBits(my));
        w.U(mb, 5); w.S(mx, mb); w.S(my, mb);
        w.U(hole ? 1 : 0, 1);       // fill style 0 (left)
        w.U(hole ? 0 : 1, 1);       // fill style 1 (right)
        for (std::size_t i = 1; i <= pts.size(); ++i) {
            const auto& a = pts[i - 1];
            const auto& b = pts[i % pts.size()];
            const int dx = (b.first - a.first) * S, dy = (b.second - a.second) * S;
            const unsigned nb = (std::max)({SignedBits(dx), SignedBits(dy), 2u});
            w.U(1, 1); w.U(1, 1);   // edge, straight
            w.U(nb - 2, 4);
            w.U(1, 1);              // general line
            w.S(dx, nb); w.S(dy, nb);
        }
    };
    // Clockwise on screen (y down): the inside is on the right of every edge.
    path({{-13, -13}, {8, -13}, {13, -8}, {13, 13}, {-13, 13}}, false);
    path({{-7, -10}, {5, -10}, {5, -4}, {-7, -4}}, true);    // shutter
    path({{-9, 1}, {9, 1}, {9, 10}, {-9, 10}}, true);        // label
    w.U(0, 6);                      // end of shape
    w.Flush();
    return p;
}

unsigned DefinedId(const std::uint8_t* b, const Tag& t) {
    switch (t.code) {
    case 2: case 6: case 7: case 10: case 11: case 13: case 14: case 20: case 21: case 22: case 32: case 33: case 34:
    case 35: case 36: case 37: case 39: case 46: case 48: case 60: case 75: case 83: case 84: case 87: case 90: case 148:
        return t.end - t.payload >= 2 ? U16(b + t.payload) : 0;
    default:
        return 0;
    }
}

} // namespace

RowLayout Layout() {
    // buttonchoices sits at x=150 in the main menu; buttons are 81.2 px wide.
    return {25.5f, 106.0f, 110.8f, 192.0f, 198.5f, 279.5f};
}

bool Build(const std::uint8_t* menu, std::size_t bytes, std::vector<std::uint8_t>& output, std::string& error) {
    if (!menu || bytes < 13 || bytes > kLimit || std::memcmp(menu, "FWS", 3) != 0) {
        error = "not an uncompressed SWF";
        return false;
    }
    const std::size_t first = 8 + (5 + 4 * (menu[8] >> 3) + 7) / 8 + 4;
    Tag redShape {}, sprite {};
    unsigned maxId = 0;
    bool haveRedShape = false, haveSprite = false;
    for (std::size_t p = first;;) {
        Tag t;
        if (!ReadTag(menu, bytes, p, t)) { error = "truncated tag stream"; return false; }
        maxId = (std::max)(maxId, DefinedId(menu, t));
        if (t.code == 2 && U16(menu + t.payload) == kRedButtonShape) { redShape = t; haveRedShape = true; }
        if (t.code == kTagDefineSprite && U16(menu + t.payload) == kButtonChoices) { sprite = t; haveSprite = true; }
        if (t.code == kTagEnd) break;
        p = t.end;
    }
    if (!haveRedShape || !haveSprite || maxId + 3 > 0xFFFF) {
        error = "buttonchoices or the red button shape not found";
        return false;
    }
    if (redShape.begin > sprite.begin) {
        error = "red button shape is defined after buttonchoices";
        return false;
    }
    const unsigned greenId = maxId + 1, slotsId = maxId + 2, iconId = maxId + 3;

    // Pass 1: find the original placements and check the sprite looks as expected.
    Placement red, green, redIcon, wrench, wide, narrow;
    bool frame8Remove = false;
    {
        unsigned frame = 1;
        for (std::size_t p = sprite.payload + 4; p < sprite.end;) {
            Tag t;
            if (!ReadTag(menu, sprite.end, p, t)) { error = "buttonchoices is damaged"; return false; }
            p = t.end;
            if (t.code == kTagPlaceObject2) {
                const Placement pl = ReadPlacement(menu, t);
                if (pl.simple) {
                    if (frame == 1 && pl.depth == kDepthRed && pl.character == kRedButtonSprite) red = pl;
                    if (frame == 1 && pl.depth == kDepthGreen && pl.character == kGreenButtonSprite) green = pl;
                    if (frame == 1 && pl.depth == kDepthRedIcon && pl.character == kRedIcon) redIcon = pl;
                    if (frame == 1 && pl.depth == kDepthWrench && pl.character == kWrenchIcon) wrench = pl;
                    if (frame == 4 && pl.depth == kDepthHighlight && pl.character == kWideHighlight) wide = pl;
                    if (frame == 5 && pl.depth == kDepthHighlight && pl.character == kNarrowHighlight) narrow = pl;
                }
                if (frame == 1 && (pl.depth == kDepthSlots || pl.depth == kDepthSlotsIcon)) {
                    error = "buttonchoices already uses the new depths";
                    return false;
                }
            }
            if (t.code == kTagRemoveObject2 && frame == 8 && t.end - t.payload == 2 &&
                U16(menu + t.payload) == kDepthHighlight) {
                frame8Remove = true;
            }
            if (t.code == kTagShowFrame || t.code == kTagCastleFrame) ++frame;
        }
    }
    if (!red.depth || !green.depth || !redIcon.depth || !wrench.depth || !wide.depth || !narrow.depth || !frame8Remove) {
        error = "buttonchoices does not match the supported layout";
        return false;
    }

    // Pass 2: rewrite.
    std::vector<std::uint8_t> resolved(menu + sprite.payload, menu + sprite.payload + 4);
    {
        unsigned frame = 1;
        auto place = [&](unsigned depth, unsigned character, std::int32_t tx, std::int32_t ty, int ratio = -1) {
            PutTag(resolved, kTagPlaceObject2, PlacementPayload(depth, character, tx, ty, ratio));
        };
        for (std::size_t p = sprite.payload + 4; p < sprite.end;) {
            Tag t;
            ReadTag(menu, sprite.end, p, t);
            p = t.end;
            bool keep = true;
            if (t.code == kTagPlaceObject2) {
                const Placement pl = ReadPlacement(menu, t);
                if (pl.simple && frame == 1 && pl.depth == kDepthRed && pl.character == kRedButtonSprite) {
                    place(kDepthRed, kRedButtonSprite, red.tx - kStepTwips, red.ty);
                    keep = false;
                } else if (pl.simple && frame == 1 && pl.depth == kDepthGreen && pl.character == kGreenButtonSprite) {
                    place(kDepthGreen, greenId, red.tx + kGreenShift, red.ty);
                    keep = false;
                } else if (pl.simple && frame == 1 && pl.depth == kDepthRedIcon && pl.character == kRedIcon) {
                    place(kDepthRedIcon, kRedIcon, redIcon.tx - kStepTwips, redIcon.ty);
                    keep = false;
                } else if (pl.simple && frame == 1 && pl.depth == kDepthWrench && pl.character == kWrenchIcon) {
                    place(kDepthWrench, kWrenchIcon, wrench.tx + kWrenchShift, wrench.ty);
                    keep = false;
                } else if (pl.simple && frame == 4 && pl.depth == kDepthHighlight && pl.character == kWideHighlight) {
                    place(kDepthHighlight, kNarrowHighlight, narrow.tx + kGreenShift, narrow.ty,
                          pl.hasRatio ? static_cast<int>(pl.ratio) : -1);
                    keep = false;
                } else if (pl.simple && frame == 5 && pl.depth == kDepthHighlight && pl.character == kNarrowHighlight) {
                    place(kDepthHighlight, kNarrowHighlight, narrow.tx - kStepTwips, narrow.ty,
                          pl.hasRatio ? static_cast<int>(pl.ratio) : -1);
                    keep = false;
                }
            }
            const bool frameEnd = t.code == kTagShowFrame || t.code == kTagCastleFrame;
            if (frameEnd && frame == 1) {
                place(kDepthSlots, slotsId, red.tx, red.ty);
                place(kDepthSlotsIcon, iconId, red.tx + kSlotsIconDx, red.ty + kSlotsIconDy);
            }
            if (keep) resolved.insert(resolved.end(), menu + t.begin, menu + t.end);
            if (t.code == kTagRemoveObject2 && frame == 8 && t.end - t.payload == 2 &&
                U16(menu + t.payload) == kDepthHighlight) {
                place(kDepthHighlight, kNarrowHighlight, narrow.tx, narrow.ty, narrow.hasRatio ? 7 : -1);
            }
            if (frameEnd) ++frame;
        }
    }

    std::vector<std::uint8_t> greenShape, slotsShape;
    if (!RecoloredShape(menu, redShape, greenId, kHueGreen, greenShape) ||
        !RecoloredShape(menu, redShape, slotsId, kHueTeal, slotsShape)) {
        error = "red button shape has unsupported styles";
        return false;
    }

    output.clear();
    output.reserve(bytes + resolved.size() + greenShape.size() * 2 + 512);
    output.insert(output.end(), menu, menu + sprite.begin);
    PutTag(output, 2, greenShape);
    PutTag(output, 2, slotsShape);
    PutTag(output, 2, SlotIconShape(iconId));
    PutTag(output, kTagDefineSprite, resolved);
    output.insert(output.end(), menu + sprite.end, menu + bytes);
    return true;
}

#ifdef _WIN32
namespace {
std::atomic<bool> g_Patched {false};
std::mutex g_BufferLock;
std::vector<std::unique_ptr<std::vector<std::uint8_t>>> g_Buffers;

std::size_t MeasurePhysical(const std::uint8_t* b) {
    __try {
        if (!b || std::memcmp(b, "FWS", 3) != 0) return 0;
        std::size_t p = 8 + (5 + 4 * (b[8] >> 3) + 7) / 8 + 4;
        for (Tag t {}; p < kLimit; p = t.end) {
            if (!ReadTag(b, kLimit, p, t)) return 0;
            if (t.end > t.payload) { volatile auto touched = b[t.end - 1]; (void)touched; }
            if (!t.code) return t.end;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return 0;
}
} // namespace

bool PatchScene(void* rawScene) {
    auto* scene = static_cast<SWFScene*>(rawScene);
    if (!scene || !scene->swfBuffer) return false;
    const std::size_t bytes = MeasurePhysical(scene->swfBuffer);
    if (!bytes) return false;
    auto patched = std::make_unique<std::vector<std::uint8_t>>();
    std::string error;
    if (!Build(scene->swfBuffer, bytes, *patched, error)) {
        Core::Logger::Warn("Save slot button: MENU not patched (%s)", error.c_str());
        return false;
    }
    scene->swfBuffer = patched->data();
    {
        std::lock_guard<std::mutex> guard(g_BufferLock);
        g_Buffers.push_back(std::move(patched));
    }
    g_Patched.store(true, std::memory_order_release);
    Core::Logger::Info("Save slot button: added to the main menu row of MENU");
    return true;
}

bool IsPatched() {
    return g_Patched.load(std::memory_order_acquire);
}
#endif

} // namespace HookCrashers::SWF::SaveSlotButtonAsset
