// Runs the production loader against local SWF fixtures without launching the game.
// Detours and online asset import are stubbed; character patching is real.
#define HC_ENABLE_LOBBY_COK6_RUNTIME_DUMP 0
#include "../src/SWF/Cok6LoadHook.cpp"
#include <cstdlib>

namespace {
int menuCalls = 0, parseCalls = 0, attachCalls = 0;
LONG commitResult = NO_ERROR;
void Check(bool condition, const char* message) {
    if (!condition) { std::printf("FAIL %s\n", message); std::exit(1); }
}
int __fastcall OriginalParser(int, void*) { ++parseCalls; return 123; }
std::vector<std::uint8_t> Read(const char* path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
}

extern "C" LONG WINAPI DetourTransactionBegin() { return NO_ERROR; }
extern "C" LONG WINAPI DetourTransactionAbort() { return NO_ERROR; }
extern "C" LONG WINAPI DetourTransactionCommit() { return commitResult; }
extern "C" LONG WINAPI DetourUpdateThread(HANDLE) { return NO_ERROR; }
extern "C" LONG WINAPI DetourAttach(PVOID*, PVOID) { ++attachCalls; return NO_ERROR; }
extern "C" LONG WINAPI DetourDetach(PVOID*, PVOID) { return NO_ERROR; }

namespace HookCrashers::Core::Logger {
void Info(const char*, ...) {}
void Warn(const char*, ...) {}
void Error(const char*, ...) {}
}
namespace HookCrashers::SWF::OnlineMenuAssets {
bool PatchScene(void*) { ++menuCalls; return true; }
}

int main(int argc, char** argv) {
    Check(argc == 3, "stock MAIN and tester MAIN fixture paths required");
    using namespace HookCrashers::SWF;
    auto& registry = HookCrashers::Characters::Registry::Instance();
    for (int fixture = 1; fixture <= 2; ++fixture) {
        const auto original = Read(argv[fixture]);
        Check(original.size() > 100, "fixture exists");
        registry.Clear();
        Cok6LoadHook::Remove();
        g_OwnedPatchedBuffers.clear();
        const int initialAttaches = attachCalls;
        Check(Cok6LoadHook::Install(false), "menu-only installation succeeds");
        Check(!g_CharacterPatchesEnabled.load(), "menu startup does not enable character patches");
        g_OriginalSWFSceneParseHeader = reinterpret_cast<SWFSceneParseHeaderFn>(OriginalParser);
        auto bytes = original;
        SWFScene scene {};
        scene.swfBuffer = bytes.data();
        g_CurrentCok6Path = "../main/main.cok6";
        const auto beforeParses = parseCalls;
        Check(DetouredSWFSceneParseHeader(reinterpret_cast<int>(&scene), nullptr) == 123,
              "original parser still runs");
        Check(parseCalls == beforeParses + 1 && scene.swfBuffer == bytes.data() && bytes == original,
              "menu-only startup preserves MAIN byte-for-byte");
        Check(g_OwnedPatchedBuffers.empty(), "menu-only MAIN allocates no replacement buffer");
        // Also check that lobby character selectors are untouched in menu-only mode.
        g_CurrentCok6Path = "../lobby/lobby.cok6";
        DetouredSWFSceneParseHeader(reinterpret_cast<int>(&scene), nullptr);
        Check(scene.swfBuffer == bytes.data() && bytes == original,
              "menu-only startup preserves lobby input");
        g_CurrentCok6Path = "../menu/menu.cok6";
        const auto beforeMenus = menuCalls;
        DetouredSWFSceneParseHeader(reinterpret_cast<int>(&scene), nullptr);
        Check(menuCalls == beforeMenus + 1, "menu injection still dispatched with zero addons");

        // Matches the pre-5.1.1 public Lua/C++ explicit-install behavior.
        Check(Cok6LoadHook::Install(), "explicit character installation upgrades existing hook");
        Check(g_CharacterPatchesEnabled.load() && attachCalls == initialAttaches + 2,
              "upgrade enables patches without double attaching");
        Check(Cok6LoadHook::Install(false) && g_CharacterPatchesEnabled.load(),
              "subsequent menu request cannot disable an installed expansion");
        g_CurrentCok6Path = "../main/main.cok6";
        DetouredSWFSceneParseHeader(reinterpret_cast<int>(&scene), nullptr);
        Check(scene.swfBuffer != bytes.data(), "explicit zero-addon installation retains historical replacement");
        auto expected = BuildDefineInitSaveSystemAction();
        auto view = MakeSWFBufferView(scene.swfBuffer);
        Check(view.valid && std::search(view.base, view.base + view.fileLength,
              expected.begin(), expected.end()) != view.base + view.fileLength,
              "real MAIN contains current generated initializer after explicit installation");

        Cok6LoadHook::Remove();
        Check(!g_CharacterPatchesEnabled.load(), "removal resets character activation");
        HookCrashers::Characters::AddonCharacter addon;
        addon.name = "ActivationFixture";
        Check(registry.RegisterCharacter(addon), "register addon");
        Check(Cok6LoadHook::Install(), "automatic character-expansion call enables patches");
        g_OriginalSWFSceneParseHeader = reinterpret_cast<SWFSceneParseHeaderFn>(OriginalParser);
        g_CurrentCok6Path = "../main/main.cok6";
        scene.swfBuffer = bytes.data();
        DetouredSWFSceneParseHeader(reinterpret_cast<int>(&scene), nullptr);
        expected = BuildDefineInitSaveSystemAction();
        view = MakeSWFBufferView(scene.swfBuffer);
        Check(scene.swfBuffer != bytes.data() && std::search(view.base, view.base + view.fileLength,
              expected.begin(), expected.end()) != view.base + view.fileLength,
              "registered addon retains expanded initializer in actual MAIN");
        Cok6LoadHook::Remove();
        std::printf("PASS fixture %d: original MAIN preserved for menus; explicit and addon expansion retained\n", fixture);
    }
    commitResult = ERROR_INVALID_OPERATION;
    Check(!Cok6LoadHook::Install() && !g_CharacterPatchesEnabled.load() &&
          !g_OriginalSWFSceneParseHeader && !g_OriginalSWFNodeCtorLoadCOK6,
          "failed installation leaves character patching disabled and can be retried");
    commitResult = NO_ERROR;
    Check(Cok6LoadHook::Install(false) && !g_CharacterPatchesEnabled.load(), "menu retry succeeds without character patches");
    Cok6LoadHook::Remove();
    std::puts("PASS installation failure/retry and activation lifecycle");
}
