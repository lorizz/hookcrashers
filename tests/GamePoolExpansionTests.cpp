#include "../src/Core/GamePoolExpansionInternal.h"
#ifdef HC_POOL_HOOK_INTEGRATION
#include "../src/Core/GamePoolExpansion.h"
#include "../src/Core/Logger.h"
#include "../src/NativeDB/Natives.h"
#endif
#include <atomic>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace HookCrashers::Core::GamePoolExpansion::Detail;
constexpr std::uint32_t MiB = 1024 * 1024;

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

struct NativeFixture {
    Functions functions;
    void* code[3] {};
    std::uint32_t sizes[3] {};
    std::uint32_t resetFixups[2] {};
    explicit NativeFixture(const char* path) {
        FILE* file = nullptr;
        CHECK(fopen_s(&file, path, "rb") == 0);
        char magic[8]; std::uint32_t fixups;
        CHECK(std::fread(magic, 1, 8, file) == 8 && std::memcmp(magic, "HCPOOL1", 7) == 0);
        CHECK(std::fread(sizes, 4, 3, file) == 3 && std::fread(&fixups, 4, 1, file) == 1);
        CHECK(fixups == 2);
        for (int i = 0; i < 3; ++i) {
            CHECK(sizes[i] > 0 && sizes[i] < 4096);
            code[i] = VirtualAlloc(nullptr, sizes[i], MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            CHECK(code[i] && std::fread(code[i], 1, sizes[i], file) == sizes[i]);
        }
        for (unsigned i = 0; i < fixups; ++i) {
            std::uint32_t offset;
            CHECK(std::fread(&offset, 4, 1, file) == 1 && offset + 4 <= sizes[2]);
            resetFixups[i] = offset;
            const auto next = reinterpret_cast<std::uintptr_t>(code[2]) + offset + 4;
            const std::uint32_t displacement = reinterpret_cast<std::uintptr_t>(&std::memset) - next;
            std::memcpy(static_cast<std::uint8_t*>(code[2]) + offset, &displacement, 4);
        }
        std::fclose(file);
        for (int i = 0; i < 3; ++i) {
            DWORD oldProtect;
            CHECK(VirtualProtect(code[i], sizes[i], PAGE_EXECUTE_READ, &oldProtect));
            CHECK(FlushInstructionCache(GetCurrentProcess(), code[i], sizes[i]));
        }
        functions = {reinterpret_cast<AllocateFn>(code[0]), reinterpret_cast<FreeFn>(code[1]),
                     reinterpret_cast<ResetFn>(code[2])};
    }
    void CopyIntoImage(std::uint8_t* image) {
        const std::uint32_t offsets[] {0xDFE70, 0xDFF70, 0xDFDF0};
        for (int i = 0; i < 3; ++i) std::memcpy(image + offsets[i], code[i], sizes[i]);
        for (auto offset : resetFixups) {
            const auto next = reinterpret_cast<std::uintptr_t>(image + offsets[2] + offset + 4);
            const std::uint32_t displacement = reinterpret_cast<std::uintptr_t>(&std::memset) - next;
            std::memcpy(image + offsets[2] + offset, &displacement, 4);
        }
        DWORD oldProtect;
        CHECK(VirtualProtect(image + 0xDF000, 0x2000, PAGE_EXECUTE_READ, &oldProtect));
        CHECK(FlushInstructionCache(GetCurrentProcess(), image + 0xDF000, 0x2000));
    }
    ~NativeFixture() { for (void* block : code) VirtualFree(block, 0, MEM_RELEASE); }
};

struct MainPool {
    Pool pool {};
    explicit MainPool(Functions& functions, std::uint32_t bytes = 128 * 1024) {
        pool.storage = ReservePages(bytes);
        CHECK(pool.storage);
        pool.begin = static_cast<std::uint8_t*>(pool.storage);
        pool.end = pool.begin + bytes;
        pool.capacity = bytes;
        pool.alignment = 32;
        functions.reset(&pool);
    }
    ~MainPool() { ReleasePages(pool.storage); }
};

void* FailReserve(std::size_t) { return nullptr; }

#ifndef HC_POOL_HOOK_INTEGRATION
int main(int argc, char** argv) {
    CHECK(argc == 2);
    NativeFixture native(argv[1]);
    auto& functions = native.functions;
    {
        MainPool main(functions);
        Expansion expanded(&functions);
        expanded.Configure(main.pool.capacity + 64 * MiB);
        for (int size = 0; size < 130; ++size) {
            void* pointer = expanded.Allocate(&main.pool, size, true);
            CHECK(pointer && reinterpret_cast<std::uintptr_t>(pointer) % 32 == 0);
            CHECK(!expanded.Free(pointer));
            functions.free(&main.pool, pointer);
        }
        CHECK(expanded.ArenaCount() == 0);
        CHECK(!expanded.Allocate(&main.pool, -1, true));
        CHECK(!expanded.Allocate(&main.pool, INT_MAX, true));
        CHECK(!expanded.Allocate(&main.pool, 16 * MiB, false));
        std::puts("PASS original allocations, alignment, secondary-pool isolation, overflow guards");

        const auto originalBegin = main.pool.begin;
        const auto originalEnd = main.pool.end;
        void* pointer = expanded.Allocate(&main.pool, 16 * MiB, true);
        CHECK(pointer && expanded.ArenaCount() == 1 && expanded.ExtraBytes() == 64 * MiB);
        std::memset(pointer, 0, 16 * MiB); // Exact failing operation from both tester reports.
        CHECK(main.pool.begin == originalBegin && main.pool.end == originalEnd);
        CHECK(main.pool.capacity == 128 * 1024);
        CHECK(expanded.Free(pointer));
        CHECK(expanded.Free(pointer)); // Native double-free guard remains intact.
        std::puts("PASS 16 MiB allocation + zero-fill after original-pool failure; old pointers unchanged");

        void* blocks[3];
        for (auto& block : blocks) { block = expanded.Allocate(&main.pool, 20 * MiB, true); CHECK(block); }
        CHECK(!expanded.Allocate(&main.pool, 20 * MiB, true));
        CHECK(expanded.ExtraBytes() == 64 * MiB);
        CHECK(expanded.Free(blocks[1]));
        CHECK(expanded.Free(blocks[0]));
        CHECK(expanded.Free(blocks[2]));
        pointer = expanded.Allocate(&main.pool, 48 * MiB, true);
        CHECK(pointer && expanded.ArenaCount() == 1);
        CHECK(expanded.Free(pointer));
        std::puts("PASS budget cap, fragmentation/coalescing, and arena reuse with native free routine");

        std::atomic<bool> success {true};
        std::vector<std::thread> threads;
        for (unsigned thread = 0; thread < 4; ++thread) {
            threads.emplace_back([&] {
                for (unsigned i = 0; i < 32; ++i) {
                    void* block = expanded.Allocate(&main.pool, 8 * MiB, true);
                    if (!block) { success = false; return; }
                    std::memset(block, 0xA5, 64);
                    if (!expanded.Free(block)) success = false;
                }
            });
        }
        for (auto& thread : threads) thread.join();
        CHECK(success.load());
        std::puts("PASS concurrent overflow allocation/free");
        expanded.Reset(&main.pool);
        CHECK(expanded.ExtraBytes() == 0 && expanded.ArenaCount() == 0);
        pointer = expanded.Allocate(&main.pool, 16 * MiB, true);
        CHECK(pointer && expanded.ArenaCount() == 1);
        CHECK(expanded.Free(pointer));
        expanded.Reset(&main.pool);
        std::puts("PASS reset releases arenas and allows subsequent growth");
    }
    {
        MainPool main(functions);
        Expansion expanded(&functions, nullptr, FailReserve);
        expanded.Configure(main.pool.capacity + 64 * MiB);
        CHECK(!expanded.Allocate(&main.pool, 16 * MiB, true));
        CHECK(expanded.ExtraBytes() == 0 && expanded.ArenaCount() == 0);
        CHECK(main.pool.capacity == 128 * 1024);
        expanded.Reset(&main.pool);
        std::puts("PASS OS allocation failure preserves the original pool and budget");
    }
    {
        MainPool main(functions);
        Expansion expanded(&functions);
        expanded.Configure(main.pool.capacity + 128 * MiB);
        void* first = expanded.Allocate(&main.pool, 40 * MiB, true);
        void* second = expanded.Allocate(&main.pool, 40 * MiB, true);
        CHECK(first && second && expanded.ArenaCount() == 2);
        CHECK(!expanded.Allocate(&main.pool, 40 * MiB, true));
        CHECK(expanded.Free(first) && expanded.Free(second));
        expanded.Reset(&main.pool);
        std::puts("PASS multiple arenas and aggregate budget enforcement");
    }
    std::puts("ALL OFFLINE NATIVE ALLOCATOR TESTS PASSED (no game process launched)");
    return 0;
}
#else
int main(int argc, char** argv) {
    CHECK(argc == 2);
    NativeFixture native(argv[1]);
    HookCrashers::Core::Logger::Initialize(GetModuleHandleA(nullptr));
    auto* image = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x400000,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(image);
    native.CopyIntoImage(image);
    HookCrashers::NativeDB::SetGameBase(reinterpret_cast<std::uintptr_t>(image));
    Functions game {reinterpret_cast<AllocateFn>(image + 0xDFE70),
                    reinterpret_cast<FreeFn>(image + 0xDFF70),
                    reinterpret_cast<ResetFn>(image + 0xDFDF0)};
    Pool main {};
    // Model the 576 MiB virtual range while committing only its first page.
    // The unavailable free list simulates the tester's exhausted/fragmented pool.
    main.storage = VirtualAlloc(nullptr, 576 * MiB, MEM_RESERVE, PAGE_READWRITE);
    CHECK(main.storage && VirtualAlloc(main.storage, 0x10000, MEM_COMMIT, PAGE_READWRITE));
    main.begin = static_cast<std::uint8_t*>(main.storage);
    main.end = main.begin + 576 * MiB;
    main.capacity = 576 * MiB;
    main.alignment = 32;
    game.reset(&main);
    void* oldAllocation = game.allocate(&main, 64);
    CHECK(oldAllocation);
    std::memset(oldAllocation, 0x37, 64);
    main.freeHead = nullptr;
    *reinterpret_cast<Pool**>(image + 0x361138) = &main;
    image[0x360EE1] = 1;
    CHECK(HookCrashers::Core::GamePoolExpansion::Install(1024));
    void* pixels = game.allocate(&main, 16 * MiB);
    CHECK(pixels);
    std::memset(pixels, 0, 16 * MiB);
    CHECK(*static_cast<std::uint8_t*>(oldAllocation) == 0x37);
    Pool differentThreadPool {};
    game.free(&differentThreadPool, pixels); // Must be rerouted before native range rejection.
    std::puts("PASS actual Detours allocate/free hooks, cross-pool free, 16 MiB zero-fill, old allocation preserved");
    void* allocations[7];
    for (auto& pointer : allocations) { pointer = game.allocate(&main, 40 * MiB); CHECK(pointer); }
    CHECK(!game.allocate(&main, 40 * MiB)); // 576 + 7*64 = 1024 MiB aggregate capacity.
    for (void* pointer : allocations) game.free(&differentThreadPool, pointer);
    game.free(&main, oldAllocation);
    CHECK(main.capacity == 576 * MiB);
    game.reset(&main);
    for (void* pointer : allocations) {
        MEMORY_BASIC_INFORMATION region {};
        CHECK(VirtualQuery(pointer, &region, sizeof(region)) && region.State == MEM_FREE);
    }
    std::puts("PASS real hook routing reaches 1 GiB budget and reset releases every overflow arena");
    main.freeHead = nullptr;
    pixels = game.allocate(&main, 16 * MiB);
    CHECK(pixels);
    game.free(&main, pixels);
    game.reset(&main);
    HookCrashers::Core::GamePoolExpansion::Shutdown();
    main.freeHead = nullptr;
    CHECK(!game.allocate(&main, 16 * MiB)); // Detach restored the unexpanded native behavior.
    std::puts("PASS growth after reset and clean hook detach");
    ReleasePages(main.storage);
    HookCrashers::NativeDB::SetGameBase(0);
    ReleasePages(image);
    HookCrashers::Core::Logger::Shutdown();
    std::puts("ALL OFFLINE HOOK INTEGRATION TESTS PASSED (isolated routines only; no game launched)");
    return 0;
}
#endif
