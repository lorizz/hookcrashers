// Standalone: clang++ -std=c++17 tests/MenuGraphicAssetsTests.cpp src/SWF/MenuGraphicAssets.cpp && ./a.out
#include "../src/SWF/MenuGraphicAssets.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using Bytes = std::vector<std::uint8_t>;
namespace Assets = HookCrashers::SWF::MenuGraphicAssets;

namespace {
int g_checks = 0;
void Check(bool good, const char* message) {
    ++g_checks;
    if (!good) {
        std::printf("FAIL %s\n", message);
        std::exit(1);
    }
}

void Put16(Bytes& b, unsigned v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); }
void Tag(Bytes& out, unsigned code, const Bytes& payload) {
    Put16(out, (code << 6) | 63);
    const std::uint32_t n = static_cast<std::uint32_t>(payload.size());
    for (int i = 0; i < 4; ++i) out.push_back((n >> (8 * i)) & 255);
    out.insert(out.end(), payload.begin(), payload.end());
}
Bytes Shape(unsigned id) { Bytes p; Put16(p, id); p.insert(p.end(), {0x00, 0x00, 0x00, 0x00, 0x00}); return p; }
Bytes Sprite(unsigned id, const Bytes& inner) { Bytes p; Put16(p, id); Put16(p, 1); p.insert(p.end(), inner.begin(), inner.end()); return p; }
Bytes Place2(unsigned depth, unsigned character) { Bytes p {0x02}; Put16(p, depth); Put16(p, character); return p; }
Bytes Export(unsigned id, const char* name) {
    Bytes p; Put16(p, 1); Put16(p, id);
    p.insert(p.end(), name, name + std::strlen(name) + 1);
    return p;
}
Bytes Swf(const Bytes& tags, const char* signature = "FWS") {
    Bytes b(signature, signature + 3);
    b.push_back(8);                  // version
    b.insert(b.end(), 4, 0);         // FileLength placeholder
    b.push_back(0x00);               // RECT with nbits = 0
    Put16(b, 0x1800);                // frame rate
    Put16(b, 1);                     // frame count
    b.insert(b.end(), tags.begin(), tags.end());
    Tag(b, 0, {});
    const std::uint32_t n = static_cast<std::uint32_t>(b.size());
    for (int i = 0; i < 4; ++i) b[4 + i] = (n >> (8 * i)) & 255;
    return b;
}

Bytes LogoSwf(unsigned placeTag = 26) {
    Bytes inner;
    if (placeTag == 26) Tag(inner, 26, Place2(1, 1));
    else { Bytes p {0x06}; Put16(p, 1); Put16(p, 1); Tag(inner, placeTag, p); }
    Tag(inner, 1, {});
    Tag(inner, 0, {});
    Bytes tags;
    Tag(tags, 2, Shape(1));
    Tag(tags, 39, Sprite(2, inner));
    Tag(tags, 56, Export(2, "logo"));
    return Swf(tags);
}

Bytes MenuSwf() {
    Bytes tags;
    Tag(tags, 2, Shape(5));
    Tag(tags, 56, Export(5, "stock"));
    return Swf(tags);
}

struct Found { bool exported = false; unsigned exportId = 0; unsigned placed = 0; unsigned shapes = 0; std::uint32_t fileLength = 0; bool fileLengthOk = false; };

Found Inspect(const Bytes& swf, const char* alias) {
    Found f;
    f.fileLength = swf[4] | (swf[5] << 8) | (swf[6] << 16) | (std::uint32_t(swf[7]) << 24);
    f.fileLengthOk = f.fileLength == swf.size();
    std::size_t p = 8 + 1 + 4;
    while (p + 2 <= swf.size()) {
        const unsigned word = swf[p] | (swf[p + 1] << 8);
        const unsigned code = word >> 6;
        std::size_t len = word & 63, payload = p + 2;
        if (len == 63) { len = swf[payload] | (swf[payload + 1] << 8) | (swf[payload + 2] << 16) | (std::uint32_t(swf[payload + 3]) << 24); payload += 4; }
        if (code == 0) break;
        if (code == 2) ++f.shapes;
        if (code == 56 && std::strcmp(reinterpret_cast<const char*>(&swf[payload + 4]), alias) == 0) {
            f.exported = true;
            f.exportId = swf[payload + 2] | (swf[payload + 3] << 8);
        }
        if (code == 39) {
            const unsigned spriteId = swf[payload] | (swf[payload + 1] << 8);
            if (spriteId == f.exportId || !f.exportId) {
                std::size_t q = payload + 4;
                const unsigned w = swf[q] | (swf[q + 1] << 8);
                if ((w >> 6) == 26) f.placed = swf[q + 2 + 4 + 3] | (swf[q + 2 + 4 + 4] << 8);
            }
        }
        p = payload + len;
    }
    return f;
}
} // namespace

int main() {
    const Bytes logo = LogoSwf();
    const Bytes menu = MenuSwf();
    std::string error;

    Check(Assets::Validate(logo.data(), logo.size(), "logo", error), "valid vector sprite accepted");
    Check(!Assets::Validate(logo.data(), logo.size(), "missing", error) && error.find("not found") != std::string::npos, "missing export reported");
    Check(Assets::Validate(logo.data(), logo.size(), "#2", error), "sprite selected by character id");
    Check(!Assets::Validate(logo.data(), logo.size(), "#1", error) && error.find("not a sprite") != std::string::npos, "#id of a shape rejected");
    Check(!Assets::Validate(logo.data(), logo.size(), "#x", error), "malformed #id rejected");

    {
        Bytes tags; Tag(tags, 2, Shape(1)); Tag(tags, 56, Export(1, "shape"));
        const Bytes s = Swf(tags);
        Check(!Assets::Validate(s.data(), s.size(), "shape", error) && error.find("not a sprite") != std::string::npos, "non-sprite export rejected");
    }
    {
        Bytes c = logo; c[0] = 'C';
        Check(!Assets::Validate(c.data(), c.size(), "logo", error) && error.find("compressed") != std::string::npos, "compressed SWF rejected");
    }
    {
        Bytes tags; Tag(tags, 20, Bytes {1, 0, 5, 0, 0, 0, 0}); Tag(tags, 39, Sprite(2, Bytes {0, 0})); Tag(tags, 56, Export(2, "logo"));
        const Bytes s = Swf(tags);
        Check(!Assets::Validate(s.data(), s.size(), "logo", error) && error.find("bitmaps") != std::string::npos, "bitmaps rejected");
    }
    {
        const Bytes s = LogoSwf(70);
        Check(!Assets::Validate(s.data(), s.size(), "logo", error) && error.find("PlaceObject3") != std::string::npos, "PlaceObject3 rejected");
    }
    {
        Bytes inner; Tag(inner, 26, Place2(1, 9)); Tag(inner, 0, {});
        Bytes tags; Tag(tags, 39, Sprite(2, inner)); Tag(tags, 56, Export(2, "logo"));
        const Bytes s = Swf(tags);
        Check(!Assets::Validate(s.data(), s.size(), "logo", error) && error.find("not defined") != std::string::npos, "dangling placement rejected");
    }
    {
        Bytes bad = logo; bad[4] ^= 1;
        Check(!Assets::Validate(bad.data(), bad.size(), "logo", error) && error.find("FileLength") != std::string::npos, "FileLength mismatch rejected");
    }
    {
        Bytes tags; Tag(tags, 69, Bytes {0x08, 0, 0, 0}); Tag(tags, 2, Shape(1));
        Bytes inner; Tag(inner, 0, {});
        Tag(tags, 39, Sprite(2, inner)); Tag(tags, 56, Export(2, "logo"));
        const Bytes s = Swf(tags);
        Check(!Assets::Validate(s.data(), s.size(), "logo", error) && error.find("ActionScript 3") != std::string::npos, "AS3 SWF rejected");
    }

    Bytes output;
    std::vector<std::string> imported, failures;
    std::vector<Assets::Source> sources {{logo.data(), logo.size(), "logo", "hcg_0"}};
    Check(Assets::Build(menu.data(), menu.size(), sources, output, imported, failures), "build succeeds");
    Check(failures.empty() && imported.size() == 1 && imported[0] == "hcg_0", "graphic imported");
    const Found found = Inspect(output, "hcg_0");
    Check(found.fileLengthOk, "patched FileLength matches size");
    Check(found.exported && found.exportId == 7, "alias exported with remapped sprite id (after stock id 5)");
    Check(found.placed == 6, "sprite placement remapped to the new shape id");
    Check(found.shapes == 2, "stock shape kept and imported shape added");

    Bytes again;
    Check(Assets::Build(output.data(), output.size(), sources, again, imported, failures) && again.empty() && imported.size() == 1,
          "already-imported alias is not duplicated");

    Bytes keep = {1, 2, 3};
    const Bytes badLogo = LogoSwf(70);
    std::vector<Assets::Source> mixed {{badLogo.data(), badLogo.size(), "logo", "hcg_bad"}, {logo.data(), logo.size(), "logo", "hcg_ok"}};
    Check(Assets::Build(menu.data(), menu.size(), mixed, keep, imported, failures), "mixed build succeeds");
    Check(failures.size() == 1 && imported.size() == 1 && imported[0] == "hcg_ok", "bad source skipped, good source imported");
    Check(Inspect(keep, "hcg_ok").exported && !Inspect(keep, "hcg_bad").exported, "only the good alias exists");

    Bytes untouched = {9};
    std::vector<Assets::Source> onlyBad {{badLogo.data(), badLogo.size(), "logo", "hcg_bad"}};
    Check(Assets::Build(menu.data(), menu.size(), onlyBad, untouched, imported, failures) && untouched == Bytes {9},
          "output untouched when nothing imports");
    Check(!Assets::Build(menu.data(), 10, sources, untouched, imported, failures), "truncated MENU rejected");

    Assets::ClearForTests();
    Check(Assets::Register("mod.logo", logo, "logo", error) == Assets::RegisterResult::Ok, "register graphic");
    Check(Assets::Register("mod.logo", logo, "logo", error) == Assets::RegisterResult::DuplicateId, "duplicate graphic id rejected");
    Check(Assets::Register("mod.bad", badLogo, "logo", error) == Assets::RegisterResult::Unsupported, "unsupported graphic rejected at registration");
    Check(Assets::AliasFor("mod.logo") == "hcg_0" && Assets::AliasFor("nope").empty(), "alias lookup");
    Check(Assets::IsRegistered("mod.logo") && !Assets::IsAvailableInScene("mod.logo"), "registered but not yet in a scene");
    Assets::ClearForTests();

    std::printf("PASS menu graphic import (%d checks)\n", g_checks);
    return 0;
}
