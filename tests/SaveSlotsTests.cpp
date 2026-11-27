// Standalone:
//   clang++ -std=c++17 tests/SaveSlotsTests.cpp src/Save/SaveCrypto.cpp src/Save/SaveLayout.cpp src/Save/SlotMeta.cpp && ./a.out
// Optional real-data checks (never in CI, the files are private):
//   HC_TEST_EXE=<castle_wine_unpacked.exe> HC_TEST_SAVE=<cc_save.dat> HC_TEST_STEAMID=<steamid64> ./a.out
#include "../src/Save/SaveCrypto.h"
#include "../src/Save/SaveLayout.h"
#include "../src/Save/SlotMeta.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace HookCrashers::Save;
using Bytes = std::vector<std::uint8_t>;

namespace {
int g_checks = 0;
void Check(bool good, const char* message) {
    ++g_checks;
    if (!good) {
        std::printf("FAIL %s\n", message);
        std::exit(1);
    }
}

Bytes Pattern(std::size_t size, unsigned mul, unsigned add) {
    Bytes b(size);
    for (std::size_t i = 0; i < size; ++i) b[i] = static_cast<std::uint8_t>(i * mul + add);
    return b;
}

std::vector<std::uint32_t> FakeTables() {
    std::vector<std::uint32_t> t(Crypto::kInitTableWords);
    std::uint32_t x = 0x243F6A88u;
    for (auto& w : t) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; w = x; }
    return t;
}

Bytes ReadFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

// RVA -> file offset for a PE32 image (tests only).
bool ReadRva(const Bytes& pe, std::uint32_t rva, void* out, std::size_t size) {
    auto u32 = [&](std::size_t o) { std::uint32_t v; std::memcpy(&v, &pe[o], 4); return v; };
    auto u16 = [&](std::size_t o) { std::uint16_t v; std::memcpy(&v, &pe[o], 2); return v; };
    const std::size_t nt = u32(0x3C);
    const unsigned sections = u16(nt + 6);
    const std::size_t first = nt + 24 + u16(nt + 20);
    for (unsigned i = 0; i < sections; ++i) {
        const std::size_t s = first + 40 * i;
        const std::uint32_t vsize = u32(s + 8), va = u32(s + 12), raw = u32(s + 20);
        if (rva >= va && rva + size <= va + vsize) {
            std::memcpy(out, &pe[raw + rva - va], size);
            return true;
        }
    }
    return false;
}

void Fill(std::uint8_t* c, std::uint8_t v) { std::memset(c, v, Layout::kCharacterSize); }
std::uint8_t At(const Bytes& save, std::size_t slot) { return save[Layout::kGlobalSize + slot * Layout::kCharacterSize]; }

Bytes MakeSave(std::size_t addons) {
    Bytes s(Layout::SizeFor(addons), 0);
    for (std::size_t i = 0; i < Layout::kGlobalSize; ++i) s[i] = 0xA0;
    for (std::size_t c = 0; c < 32 + addons + 10; ++c) Fill(&s[Layout::kGlobalSize + c * Layout::kCharacterSize], static_cast<std::uint8_t>(c));
    for (std::size_t i = s.size() - Layout::kTailSize; i < s.size(); ++i) s[i] = 0xEE;
    Crypto::StoreChecksum(s);
    return s;
}
} // namespace

int main() {
    // --- checksum (reference values from the Python model of 0x130EE0) ---
    {
        const Bytes even = Pattern(0x850, 7, 3);
        Check(Crypto::Checksum(even.data(), even.size()) == 0x42516, "checksum, even payload");
        const Bytes odd = Pattern(0x15, 13, 1);
        Check(Crypto::Checksum(odd.data(), odd.size()) == 0xA12, "checksum, odd payload with tail byte");
        Bytes s = even;
        Crypto::StoreChecksum(s);
        Check(Crypto::HasValidChecksum(s), "stored checksum validates");
        s[100] ^= 1;
        Check(!Crypto::HasValidChecksum(s), "corruption detected");
    }

    // --- key derivation ---
    {
        std::uint8_t base[Crypto::kKeySize];
        for (std::size_t i = 0; i < sizeof(base); ++i) base[i] = static_cast<std::uint8_t>(0x40 + i);
        const auto key = Crypto::DeriveKey(base, 0x0110000112345678ull);
        Check(key[0] == 0x10 && key[1] == 0x01, "high>>16 at 0 (LE)");
        Check(key[0x13] == 0x01 && key[0x14] == 0x00, "high&0xffff at 0x13");
        Check(key[5] == 0x34 && key[6] == 0x12, "low>>16 at 5");
        Check(key[0x0F] == 0x78 && key[0x10] == 0x56, "low&0xffff at 0xF");
        Check(key[2] == 0x42 && key[0x17] == 0x57, "other bytes keep the base key");
    }

    // --- Blowfish round trip with arbitrary tables ---
    {
        const auto tables = FakeTables();
        Crypto::Key key {};
        for (std::size_t i = 0; i < key.size(); ++i) key[i] = static_cast<std::uint8_t>(i * 31);
        const Bytes original = Pattern(0x853, 5, 9); // trailing partial block
        Bytes data = original;
        Crypto::Encrypt(data, tables.data(), key);
        Check(data != original, "encryption changes the data");
        Check(std::memcmp(&data[0x850], &original[0x850], 3) == 0, "partial block untouched");
        Crypto::Decrypt(data, tables.data(), key);
        Check(data == original, "decrypt(encrypt(x)) == x");
    }

    // --- layout ---
    Check(Layout::AddonCountForSize(0x850) == 0 && Layout::AddonCountForSize(0x880) == 1, "addon count from size");
    Check(Layout::AddonCountForSize(0x851) == -1 && Layout::AddonCountForSize(0x100) == -1, "invalid sizes");
    Check((Layout::MakeKeys({"A", "B", "A", "A"}) == std::vector<std::string> {"A", "B", "A#2", "A#3"}), "unique keys");

    {
        // Vanilla -> two addons: base data, workshop and keybinds preserved, addons initialised.
        const Bytes vanilla = MakeSave(0);
        Layout::Stash stash;
        Bytes out;
        Layout::RemapReport report;
        std::string error;
        std::vector<std::size_t> inits;
        const bool ok = Layout::Remap(vanilla, {}, {"Knight", "Wizard"}, stash,
            [&](std::size_t index, std::uint8_t* c) { inits.push_back(index); Fill(c, static_cast<std::uint8_t>(0xC0 + index)); },
            out, report, error);
        Check(ok && out.size() == 0x8B0, "vanilla migrates to 2 addons");
        Check(Crypto::HasValidChecksum(out), "migrated checksum valid");
        Check(std::memcmp(out.data(), vanilla.data(), Layout::kFirstAddonOffset) == 0, "global + base characters kept");
        Check(At(out, 32) == 0xC0 && At(out, 33) == 0xC1 && inits.size() == 2, "addons initialised in order");
        Check(At(out, 34) == 32 && At(out, 43) == 41, "workshop characters moved after the addons");
        Check(out[out.size() - 5] == 0xEE && out[out.size() - Layout::kTailSize] == 0xEE, "keybinds kept");
        Check(report.created == 2 && report.kept == 0 && report.stashed == 0, "report counts");
    }
    {
        // Reorder + removal + re-add through the stash.
        const Bytes save = MakeSave(3); // A=32 B=33 C=34
        Layout::Stash stash;
        Bytes out;
        Layout::RemapReport report;
        std::string error;
        Check(Layout::Remap(save, {"A", "B", "C"}, {"C", "A"}, stash, nullptr, out, report, error), "remove B, reorder");
        Check(At(out, 32) == 34 && At(out, 33) == 32, "C and A keep their own data");
        Check(At(out, 34) == 35, "workshop shifted back");
        Check(stash.size() == 1 && stash.count("B") && stash["B"][0] == 33, "B stashed with its data");
        Check(report.kept == 2 && report.stashed == 1, "report kept/stashed");

        Bytes again;
        Check(Layout::Remap(out, {"C", "A"}, {"A", "B", "C", "D"}, stash,
                            [](std::size_t, std::uint8_t* c) { Fill(c, 0x77); }, again, report, error), "B returns");
        Check(At(again, 32) == 32 && At(again, 33) == 33 && At(again, 34) == 34 && At(again, 35) == 0x77, "B restored, D created");
        Check(stash.empty() && report.restored == 1 && report.created == 1, "stash emptied");
        Check(Crypto::HasValidChecksum(again), "checksum after round trip");

        Bytes toVanilla;
        Check(Layout::Remap(again, {"A", "B", "C", "D"}, {}, stash, nullptr, toVanilla, report, error) &&
              toVanilla.size() == 0x850 && stash.size() == 4 && At(toVanilla, 32) == 35, "back to vanilla stashes all addons");
    }
    {
        Layout::Stash stash;
        Bytes out;
        Layout::RemapReport report;
        std::string error;
        Check(!Layout::Remap(MakeSave(1), {}, {"A"}, stash, nullptr, out, report, error), "size/list mismatch rejected");
        Check(!Layout::Remap(MakeSave(2), {"A", "A"}, {"A"}, stash, nullptr, out, report, error), "duplicate keys rejected");
    }

    // --- metadata ---
    {
        Slots::SlotMeta meta;
        meta.index = 3;
        meta.name = "Partita\tcon\nTroll";
        meta.created = 1764000000;
        meta.played = 1764100000;
        meta.hcVersion = "5.3.0";
        meta.layoutKnown = true;
        meta.characters = {"Knight", "Knight#2"};
        meta.mods = {{"Test Mod", "1.0.0", true}, {"Other", "2.1", false}};
        Layout::CharacterBytes bytes {};
        bytes[0] = 0x80; bytes[47] = 0xFF;
        meta.stash["Gone"] = bytes;
        Slots::SlotMeta back;
        Check(Slots::Parse(Slots::Serialize(meta), back), "meta round trip parses");
        Check(back.index == 3 && back.name == "Partita con Troll" && back.created == 1764000000 && back.played == 1764100000,
              "fields kept, control characters removed from the name");
        Check(back.layoutKnown && back.characters == meta.characters && back.mods == meta.mods, "lists kept");
        Check(back.stash.size() == 1 && back.stash["Gone"] == bytes, "stash kept");
        Check(!Slots::Parse("name=x\n", back), "header required");
        Check(!Slots::Parse("hcslot=11\n", back), "slot index range checked");
        Check(!Slots::Parse("hcslot=1\nchar=A\nchar=A\n", back), "duplicate character keys rejected");
        Check(Slots::SaveFileName(2) == "hc_slot2.dat" && Slots::BackupFileName(2) == "hc_slot2.dat.bak" &&
              Slots::MetaFileName(10) == "hc_slot10.meta", "file names");
        Check(Slots::ClampUtf8("ab\xC3\xA8", 3) == "ab", "UTF-8 clamp does not split a character");
    }
    {
        const auto cmp = Slots::CompareMods({{"A", "1"}, {"B", "1"}, {"C", "1"}}, {{"A", "1"}, {"B", "2"}, {"D", "1"}});
        const auto sum = Slots::Summarize(cmp);
        Check(cmp.size() == 4 && cmp[0].state == Slots::ModState::Match && cmp[1].state == Slots::ModState::Outdated &&
              cmp[1].currentVersion == "2" && cmp[2].state == Slots::ModState::Missing && cmp[3].state == Slots::ModState::Added,
              "mod comparison states");
        Check(sum.match == 1 && sum.outdated == 1 && sum.missing == 1 && sum.added == 1, "summary");
        Check(!Slots::IsBlocked(cmp), "mods without characters never block");
    }
    {
        using Slots::IsBlocked;
        using Slots::CompareMods;
        const Slots::ModEntry chars {"Chars", "1", true};
        Check(IsBlocked(CompareMods({chars}, {})), "missing character mod blocks");
        Check(IsBlocked(CompareMods({chars}, {{"Chars", "2", true}})), "changed character mod blocks");
        Check(!IsBlocked(CompareMods({chars}, {chars})), "same character mod is fine");
        Check(!IsBlocked(CompareMods({}, {chars})), "adding a character mod is fine");
        Check(!IsBlocked(CompareMods({{"Lua", "1", false}}, {})), "missing plain mod is only informative");
    }

    // --- optional: real executable + real vanilla save ---
    const char* exePath = std::getenv("HC_TEST_EXE");
    const char* savePath = std::getenv("HC_TEST_SAVE");
    const char* steamIdText = std::getenv("HC_TEST_STEAMID");
    if (exePath && savePath && steamIdText) {
        const Bytes pe = ReadFile(exePath);
        std::vector<std::uint32_t> tables(Crypto::kInitTableWords);
        std::uint8_t baseKey[Crypto::kKeySize];
        Check(ReadRva(pe, 0x1C36A0, tables.data(), tables.size() * 4) && ReadRva(pe, 0x1C4700, baseKey, sizeof(baseKey)),
              "tables read from the executable");
        const auto key = Crypto::DeriveKey(baseKey, std::strtoull(steamIdText, nullptr, 10));
        const Bytes raw = ReadFile(savePath);
        Bytes plain = raw;
        Crypto::Decrypt(plain, tables.data(), key);
        Check(Crypto::HasValidChecksum(plain) && plain[Crypto::kVersionOffset] == 2, "real save decrypts (checksum, version 2)");
        Bytes migrated;
        Layout::Stash stash;
        Layout::RemapReport report;
        std::string error;
        const int addons = Layout::AddonCountForSize(plain.size());
        Check(addons >= 0, "real save size is a valid layout");
        std::vector<std::string> oldKeys;
        for (int i = 0; i < addons; ++i) oldKeys.push_back("old" + std::to_string(i));
        std::vector<std::string> newKeys = oldKeys;
        newKeys.push_back("Added");
        Check(Layout::Remap(plain, oldKeys, newKeys, stash, [](std::size_t, std::uint8_t* c) { Fill(c, 0); }, migrated, report, error),
              "real save remaps");
        Bytes encrypted = migrated;
        Crypto::Encrypt(encrypted, tables.data(), key);
        Bytes back = encrypted;
        Crypto::Decrypt(back, tables.data(), key);
        Check(back == migrated && Crypto::HasValidChecksum(back), "migrated real save re-encrypts and validates");
        Bytes reencrypted = plain;
        Crypto::Encrypt(reencrypted, tables.data(), key);
        Check(reencrypted == raw, "re-encrypting the original plaintext gives the original file");
        if (const char* outPath = std::getenv("HC_TEST_OUT")) {
            std::ofstream(outPath, std::ios::binary).write(reinterpret_cast<const char*>(encrypted.data()),
                                                           static_cast<std::streamsize>(encrypted.size()));
        }
        std::printf("real-data checks ran on %s\n", savePath);
    }

    std::printf("PASS save slots (%d checks)\n", g_checks);
    return 0;
}
