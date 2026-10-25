// Compile as x86. Input: the local unlock/insert natives extracted from a
// user-provided decrypted snapshot. Never commit/distribute the binary fixture.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <initializer_list>

namespace {
std::uint8_t saves[4][0x40 + 40 * 0x30] {};
std::uint32_t profile = 0;
void* nativeCode = nullptr;
void* insertCode = nullptr;
bool noSave = false;
void Check(bool condition, const char* message) {
    if (!condition) { std::printf("FAIL %s\n", message); std::exit(1); }
}
void* __stdcall GetSave(std::uint32_t player) {
    return !noSave && player < 4 ? saves[player] : nullptr;
}
struct Node { Node *left, *parent, *right; std::uint8_t color, nil; std::uint16_t pad; std::uint32_t key; };
struct Set { Node* head; std::uint32_t count; };
struct Pair { Node* node; std::uint8_t inserted; std::uint8_t pad[3]; };
std::uint32_t cookie = 0x12345678;
Node insertedNode {};
bool insertCallbackCalled = false;
void* expectedContext = nullptr;
Pair* __fastcall InsertDependency(Set* set, void*, Pair* output, std::uint32_t left,
                                  Node* parent, std::uint32_t* key, void* context) {
    Check(set && output && key && parent, "native insert dependency arguments");
    Check(context == expectedContext && left == 1, "fourth stack argument and insertion side");
    insertedNode.key = *key;
    output->node = &insertedNode;
    ++set->count;
    insertCallbackCalled = true;
    return output;
}

// Deliberately reproduce the old caller's extra ADD ESP,8 after the real
// native's RET 8; repair ESP before returning so the test itself stays valid.
__declspec(naked) int ProbeOldCallerCleanup() {
    __asm {
        push ebp
        mov ebp, esp
        push esi
        mov esi, esp
        push 32
        push 0
        call dword ptr [nativeCode]
        add esp, 8
        mov eax, esp
        sub eax, esi
        mov esp, esi
        pop esi
        mov esp, ebp
        pop ebp
        ret
    }
}
}

int main(int argc, char** argv) {
    Check(argc == 2, "fixture path required");
    FILE* file = nullptr;
    fopen_s(&file, argv[1], "rb");
    Check(file != nullptr, "fixture open");
    nativeCode = VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Check(nativeCode != nullptr, "code allocation");
    Check(std::fread(nativeCode, 1, 0x7A, file) == 0x7A, "fixture length");
    insertCode = VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    Check(insertCode && std::fread(insertCode, 1, 0xEC, file) == 0xEC, "insert fixture length");
    Check(std::fgetc(file) == EOF, "exact fixture length");
    std::fclose(file);
    auto* bytes = static_cast<std::uint8_t*>(nativeCode);
    Check(std::memcmp(bytes, "\x55\x8B\xEC\x8B\x0D", 5) == 0, "native prologue");
    for (unsigned offset : {0x26u, 0x62u, 0x77u})
        Check(std::memcmp(bytes + offset, "\xC2\x08\x00", 3) == 0, "native callee cleanup");
    // Replace only the profile global and save lookup dependency. The real
    // addon/no-save native body and all its RET 8 exits remain untouched.
    const auto profileAddress = reinterpret_cast<std::uint32_t>(&profile);
    std::memcpy(bytes + 5, &profileAddress, 4);
    Check(bytes[0x10] == 0xE8, "save getter call");
    const auto displacement = reinterpret_cast<std::uint32_t>(&GetSave) -
                              reinterpret_cast<std::uint32_t>(bytes + 0x15);
    std::memcpy(bytes + 0x11, &displacement, 4);
    DWORD oldProtection = 0;
    Check(VirtualProtect(nativeCode, 0x1000, PAGE_EXECUTE_READ, &oldProtection) != 0, "code protection");
    FlushInstructionCache(GetCurrentProcess(), nativeCode, 0x7A);
    using UnlockFn = std::uint8_t(__stdcall*)(std::uint32_t, std::uint32_t);
    auto unlock = reinterpret_cast<UnlockFn>(nativeCode);
    Check(ProbeOldCallerCleanup() == 8, "old cdecl mismatch shifts ESP by 8 bytes");
    std::puts("PASS reproduced old caller's +8 byte stack imbalance with the real unlock native");
    volatile std::uint32_t guard = 0xA55A5AA5;
    for (unsigned i = 0; i < 100000; ++i) {
        const unsigned player = i % 4;
        const unsigned character = 32 + (i % 8);
        saves[player][0x40 + character * 0x30] = (i & 1) ? 0x80 : 0;
        Check(unlock(player, character) == (i & 1), "addon unlock result");
    }
    Check(guard == 0xA55A5AA5, "caller frame preserved");
    noSave = true;
    Check(unlock(0, 32) == 0 && unlock(0, 0) == 1, "no-save return path");
    std::puts("PASS 100000 stdcall native invocations, four players, addon unlock bits, caller guard, and no-save exit");
    auto* insertBytes = static_cast<std::uint8_t*>(insertCode);
    Check(std::memcmp(insertBytes, "\x55\x8B\xEC\x6A\xFF\x68", 6) == 0, "insert entry point");
    Check(insertBytes[0x17] == 0xA1 && insertBytes[0x8D] == 0xE8, "insert dependencies");
    const auto cookieAddress = reinterpret_cast<std::uint32_t>(&cookie);
    std::memcpy(insertBytes + 0x18, &cookieAddress, 4);
    const auto insertDisplacement = reinterpret_cast<std::uint32_t>(&InsertDependency) -
                                    reinterpret_cast<std::uint32_t>(insertBytes + 0x92);
    std::memcpy(insertBytes + 0x8E, &insertDisplacement, 4);
    Check(VirtualProtect(insertCode, 0x1000, PAGE_EXECUTE_READ, &oldProtection) != 0, "insert code protection");
    FlushInstructionCache(GetCurrentProcess(), insertCode, 0xEC);
    Node sentinel {}, existing {};
    sentinel.nil = 1;
    sentinel.left = sentinel.parent = sentinel.right = &existing;
    existing.left = existing.right = existing.parent = &sentinel;
    existing.key = 7;
    Set roster {&sentinel, 1};
    Pair result {};
    std::uint32_t key = 7;
    expectedContext = const_cast<std::uint32_t*>(&guard);
    using InsertFn = void*(__thiscall*)(Set*, Pair*, void*, void*, void*);
    const auto insert = reinterpret_cast<InsertFn>(insertCode);
    Check(insert(&roster, &result, &key, &key, expectedContext) == &result, "native insert return object");
    Check(result.node == &existing && !result.inserted && roster.count == 1 && !insertCallbackCalled,
          "duplicate roster ID preserves existing node");
    key = 3;
    insert(&roster, &result, &key, &key, expectedContext);
    Check(insertCallbackCalled && result.inserted && result.node == &insertedNode &&
          insertedNode.key == 3 && roster.count == 2, "native new roster ID routes correctly");
    Check(guard == 0xA55A5AA5, "caller frame preserved after roster insert");
    std::puts("PASS real roster insert entry, thiscall/RET 16, duplicate ID and new ID with isolated insertion dependency");
    VirtualFree(nativeCode, 0, MEM_RELEASE);
    VirtualFree(insertCode, 0, MEM_RELEASE);
    std::puts("ALL OFFLINE LOBBY ABI TESTS PASSED (no game launched)");
}
