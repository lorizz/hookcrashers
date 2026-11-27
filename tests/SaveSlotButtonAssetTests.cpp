// Standalone: clang++ -std=c++17 tests/SaveSlotButtonAssetTests.cpp src/SWF/SaveSlotButtonAsset.cpp && ./a.out
// Real MENU (private game data, never in CI): HC_TEST_MENU=<decrypted menu.cok6 dump> [HC_TEST_OUT=<patched.swf>]
#include "../src/SWF/SaveSlotButtonAsset.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace Asset = HookCrashers::SWF::SaveSlotButtonAsset;
using Bytes = std::vector<std::uint8_t>;

namespace {
int g_checks = 0;
void Check(bool good, const char* message) {
    ++g_checks;
    if (!good) { std::printf("FAIL %s\n", message); std::exit(1); }
}
}

int main() {
    std::string error;
    Bytes out;
    const Bytes notSwf = {'C', 'W', 'S', 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    Check(!Asset::Build(notSwf.data(), notSwf.size(), out, error), "compressed/invalid movie rejected");
    // Minimal movie without buttonchoices: rejected, output untouched.
    Bytes empty = {'F', 'W', 'S', 6, 0, 0, 0, 0, 0x00, 0x00, 0x18, 0x01, 0x00, 0x00, 0x00};
    out = {1};
    Check(!Asset::Build(empty.data(), empty.size(), out, error) && out == Bytes {1}, "movie without the row rejected");
    const auto layout = Asset::Layout();
    Check(layout.greenRight < layout.redLeft && layout.redRight < layout.slotsLeft, "row hotspots do not overlap");

    if (const char* path = std::getenv("HC_TEST_MENU")) {
        std::ifstream in(path, std::ios::binary);
        const Bytes menu((std::istreambuf_iterator<char>(in)), {});
        Check(Asset::Build(menu.data(), menu.size(), out, error), error.c_str());
        Check(out.size() > menu.size(), "patched MENU is larger");
        Bytes again;
        Check(!Asset::Build(out.data(), out.size(), again, error), "patching twice is refused");
        if (const char* outPath = std::getenv("HC_TEST_OUT")) {
            std::ofstream(outPath, std::ios::binary).write(reinterpret_cast<const char*>(out.data()), out.size());
        }
        std::printf("real MENU patched: %u -> %u bytes\n", unsigned(menu.size()), unsigned(out.size()));
    }
    std::printf("PASS save slot button (%d checks)\n", g_checks);
    return 0;
}
