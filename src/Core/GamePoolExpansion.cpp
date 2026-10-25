#include "GamePoolExpansion.h"
#include "GamePoolExpansionInternal.h"
#include "Logger.h"
#include "Memory.h"
#include <detours.h>
#include <tlhelp32.h>
#include <vector>

namespace HookCrashers::Core::GamePoolExpansion {
namespace {
using namespace Detail;
constexpr std::uintptr_t kAllocateRva = 0xDFE70;
constexpr std::uintptr_t kFreeRva = 0xDFF70;
constexpr std::uintptr_t kResetRva = 0xDFDF0;
Functions g_Functions;
bool g_Installed = false;

void ReportGrowth(GrowthResult result, std::uint32_t requested, std::uint32_t capacity, std::uint32_t budget) {
    if (result == GrowthResult::Added) {
        Logger::Info("Game pool expanded: request=%u capacityMiB=%u budgetMiB=%u",
                     requested, capacity >> 20, budget >> 20);
    } else {
        Logger::Error("Game pool growth failed: reason=%u request=%u capacityMiB=%u budgetMiB=%u gle=%lu",
                      static_cast<unsigned>(result), requested, capacity >> 20, budget >> 20,
                      result == GrowthResult::ReserveFailed ? GetLastError() : 0);
    }
}
Expansion g_Expansion(&g_Functions, ReportGrowth);

bool IsMainPool(Pool* pool) {
    return *reinterpret_cast<std::uint8_t*>(Memory::Va(0x360EE1)) != 0 &&
           pool == *reinterpret_cast<Pool**>(Memory::Va(0x361138)) &&
           pool && pool->capacity == 0x24000000 && pool->alignment == 32;
}

void* __fastcall HookAllocate(Pool* pool, void*, int size) {
    return g_Expansion.Allocate(pool, size, IsMainPool(pool));
}
void __fastcall HookFree(Pool* pool, void*, void* allocation) {
    if (!g_Expansion.Free(allocation)) g_Functions.free(pool, allocation);
}
void __fastcall HookReset(Pool* pool) { g_Expansion.Reset(pool); }

bool VerifiedNativeCode() {
    const std::uint8_t allocate[] {0x55,0x8B,0xEC,0x83,0xEC,0x0C,0x53,0x56,0x8B,0xF1,0xBB,0x01,0,0,0,0x33,0xC9,0x89,0x75,0xFC,0x57,0x89,0x4D,0xF4};
    const std::uint8_t free[] {0x55,0x8B,0xEC,0x8B,0x55,0x08,0x57,0x8B,0xF9,0x3B,0x57,0x08,0x0F,0x86,0xD7,0,0,0,0x3B,0x57,0x0C,0x0F,0x83,0xCE};
    const std::uint8_t reset[] {0x57,0x8B,0xF9,0x8B,0x47,0x08,0x85,0xC0,0x74,0x71,0x89,0x47,0x10,0xC7,0x40,0x0C,0,0,0,0,0x8B,0x47,0x10,0x56};
    return std::memcmp(reinterpret_cast<void*>(Memory::Va(kAllocateRva)), allocate, sizeof(allocate)) == 0 &&
           std::memcmp(reinterpret_cast<void*>(Memory::Va(kFreeRva)), free, sizeof(free)) == 0 &&
           std::memcmp(reinterpret_cast<void*>(Memory::Va(kResetRva)), reset, sizeof(reset)) == 0;
}

// Allocator code may already be in use by worker threads. Enlist them all so
// Detours can relocate any instruction pointers inside the overwritten bytes.
bool EnlistThreads(std::vector<HANDLE>& handles) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 entry {sizeof(entry)};
    bool ok = true;
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId()) continue;
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                                       FALSE, entry.th32ThreadID);
            if (!thread) { ok = false; break; }
            handles.push_back(thread);
        } while (Thread32Next(snapshot, &entry));
    } else ok = false;
    CloseHandle(snapshot);
    // Build the handle vector before suspending any thread (no vector growth
    // while another thread might be suspended inside the CRT allocator).
    if (ok) for (HANDLE thread : handles) {
        if (DetourUpdateThread(thread) != NO_ERROR) { ok = false; break; }
    }
    return ok;
}
}

bool Install(std::uint32_t budgetMiB) {
    if (g_Installed) return true;
    if (budgetMiB == 0 || budgetMiB == 576) {
        Logger::Info("Game pool expansion disabled; retaining 576 MiB");
        return true;
    }
    if (budgetMiB < 576 || budgetMiB > 1536 || !VerifiedNativeCode()) {
        Logger::Error("Game pool expansion refused: invalid budget or unsupported allocator instructions");
        return false;
    }
    g_Expansion.Configure(budgetMiB * 1024u * 1024u);
    g_Functions.allocate = reinterpret_cast<AllocateFn>(Memory::Va(kAllocateRva));
    g_Functions.free = reinterpret_cast<FreeFn>(Memory::Va(kFreeRva));
    g_Functions.reset = reinterpret_cast<ResetFn>(Memory::Va(kResetRva));
    HMODULE pinned = nullptr;
    // Overflow allocations and their hooks must remain available until exit.
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCSTR>(&Install), &pinned)) return false;
    if (DetourTransactionBegin() != NO_ERROR) return false;
    std::vector<HANDLE> threads;
    bool ok = DetourAttach(reinterpret_cast<PVOID*>(&g_Functions.allocate), reinterpret_cast<PVOID>(HookAllocate)) == NO_ERROR;
    if (ok) ok = DetourAttach(reinterpret_cast<PVOID*>(&g_Functions.free), reinterpret_cast<PVOID>(HookFree)) == NO_ERROR;
    if (ok) ok = DetourAttach(reinterpret_cast<PVOID*>(&g_Functions.reset), reinterpret_cast<PVOID>(HookReset)) == NO_ERROR;
    if (ok) ok = EnlistThreads(threads);
    const LONG result = ok ? DetourTransactionCommit() : DetourTransactionAbort();
    for (HANDLE thread : threads) CloseHandle(thread);
    g_Installed = ok && result == NO_ERROR;
    Logger::Info("Game pool expansion install=%d originalMiB=576 budgetMiB=%u", g_Installed ? 1 : 0, budgetMiB);
    return g_Installed;
}

void Shutdown() {
    if (!g_Installed) return;
    // The module is pinned; this runs at process shutdown. Windows reclaims
    // any remaining arenas. Do not invalidate live buffers during DLL teardown.
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourDetach(reinterpret_cast<PVOID*>(&g_Functions.allocate), reinterpret_cast<PVOID>(HookAllocate));
    DetourDetach(reinterpret_cast<PVOID*>(&g_Functions.free), reinterpret_cast<PVOID>(HookFree));
    DetourDetach(reinterpret_cast<PVOID*>(&g_Functions.reset), reinterpret_cast<PVOID>(HookReset));
    if (DetourTransactionCommit() == NO_ERROR) g_Installed = false;
}
}
