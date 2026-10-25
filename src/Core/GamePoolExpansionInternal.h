#pragma once

#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace HookCrashers::Core::GamePoolExpansion::Detail {

// Layout verified from Allocate/Free/Reset in the supported x86 executable.
// Leave an extra statistics entry at the end for the highest native tag.
struct Pool {
    std::uint32_t ownsStorage;
    void* storage;
    std::uint8_t* begin;
    std::uint8_t* end;
    void* freeHead;
    std::uint32_t capacity;
    std::uint32_t tag;
    std::uint32_t tagDepth;
    std::uint32_t alignment;
    std::array<std::uint8_t, 0x6B0 - 0x24> bookkeeping;
};
static_assert(sizeof(void*) == 4, "Game allocator hooks require x86");
static_assert(offsetof(Pool, freeHead) == 0x10);
static_assert(offsetof(Pool, capacity) == 0x14);
static_assert(offsetof(Pool, tag) == 0x18);
static_assert(offsetof(Pool, alignment) == 0x20);
static_assert(sizeof(Pool) == 0x6B0);

using AllocateFn = void*(__thiscall*)(Pool*, int);
using FreeFn = void(__thiscall*)(Pool*, void*);
using ResetFn = void(__fastcall*)(Pool*);
struct Functions {
    AllocateFn allocate = nullptr;
    FreeFn free = nullptr;
    ResetFn reset = nullptr;
};

enum class GrowthResult { Added, BudgetReached, ReserveFailed, InvalidLayout };
using Reporter = void(*)(GrowthResult, std::uint32_t, std::uint32_t, std::uint32_t);
using ReserveFn = void*(*)(std::size_t);
using ReleaseFn = void(*)(void*);

inline void* ReservePages(std::size_t size) {
    return VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}
inline void ReleasePages(void* memory) {
    VirtualFree(memory, 0, MEM_RELEASE);
}

class Expansion {
public:
    explicit Expansion(Functions* functions, Reporter reporter = nullptr,
                       ReserveFn reserve = ReservePages, ReleaseFn release = ReleasePages)
        : m_Functions(functions), m_Report(reporter), m_Reserve(reserve), m_Release(release) {}

    void Configure(std::uint32_t budgetBytes) { m_Budget = budgetBytes; }

    void* Allocate(Pool* owner, int size, bool isMainPool) {
        // The native allocator's signed alignment math overflows near INT_MAX.
        if (size < 0 || static_cast<std::uint32_t>(size) > 0x7FFFFFC0u) return nullptr;
        if (void* result = m_Functions->allocate(owner, size)) {
            return result;
        }
        if (!isMainPool || size <= 0) return nullptr;

        Lock lock(m_Lock);
        if (!Valid(owner) || (m_Owner && m_Owner != owner) || owner->tag == 0 || owner->tag > 0x35) {
            Report(GrowthResult::InvalidLayout, static_cast<std::uint32_t>(size));
            return nullptr;
        }
        m_Owner = owner;
        m_BaseCapacity = owner->capacity;
        for (std::size_t i = 0; i < m_Count; ++i) {
            m_Arenas[i].pool.tag = owner->tag;
            if (void* result = m_Functions->allocate(&m_Arenas[i].pool, size)) return result;
        }

        const auto aligned = (static_cast<std::uint64_t>(size) + 31) & ~std::uint64_t{31};
        const auto needed = aligned + 32; // Native allocation header, aligned to 32 bytes.
        const auto used = static_cast<std::uint64_t>(owner->capacity) + m_ExtraBytes;
        const auto available = used < m_Budget ? m_Budget - used : 0;
        constexpr std::uint64_t chunkSize = 64ull * 1024 * 1024;
        auto capacity = needed > chunkSize ? needed : chunkSize;
        capacity = (capacity + 0xFFFF) & ~std::uint64_t{0xFFFF};
        if (capacity > available) capacity = available & ~std::uint64_t{0xFFFF};
        if (capacity < needed || m_Count == m_Arenas.size()) {
            Report(GrowthResult::BudgetReached, static_cast<std::uint32_t>(size));
            return nullptr;
        }

        void* storage = m_Reserve(static_cast<std::size_t>(capacity));
        if (!storage) {
            Report(GrowthResult::ReserveFailed, static_cast<std::uint32_t>(size));
            return nullptr;
        }
        auto& arena = m_Arenas[m_Count];
        arena.pool = {};
        arena.pool.storage = storage;
        arena.pool.begin = static_cast<std::uint8_t*>(storage);
        arena.pool.end = arena.pool.begin + capacity;
        arena.pool.capacity = static_cast<std::uint32_t>(capacity);
        arena.pool.alignment = 32;
        // The original reset builds the real native free list and statistics.
        m_Functions->reset(&arena.pool);
        arena.pool.tag = owner->tag;
        void* result = m_Functions->allocate(&arena.pool, size);
        if (!result) {
            m_Release(storage);
            arena.pool = {};
            Report(GrowthResult::InvalidLayout, static_cast<std::uint32_t>(size));
            return nullptr;
        }
        ++m_Count;
        m_ExtraBytes += static_cast<std::uint32_t>(capacity);
        Report(GrowthResult::Added, static_cast<std::uint32_t>(size));
        return result;
    }

    // Route frees by ownership, even if the caller selected a different thread's pool.
    bool Free(void* allocation) {
        if (!allocation) return false;
        Lock lock(m_Lock);
        const auto address = reinterpret_cast<std::uintptr_t>(allocation);
        for (std::size_t i = 0; i < m_Count; ++i) {
            auto& pool = m_Arenas[i].pool;
            if (address > reinterpret_cast<std::uintptr_t>(pool.begin) &&
                address < reinterpret_cast<std::uintptr_t>(pool.end)) {
                m_Functions->free(&pool, allocation);
                return true;
            }
        }
        return false;
    }

    void Reset(Pool* owner) {
        Lock lock(m_Lock);
        // Reset invalidates that pool's allocations by definition. Overflow
        // allocations have the same lifetime and can now be released as well.
        if (owner == m_Owner) {
            for (std::size_t i = 0; i < m_Count; ++i) {
                m_Release(m_Arenas[i].pool.storage);
                m_Arenas[i].pool = {};
            }
            m_Count = 0;
            m_ExtraBytes = 0;
            m_Owner = nullptr;
            m_BaseCapacity = 0;
        }
        m_Functions->reset(owner);
    }

    std::uint32_t ExtraBytes() const { return m_ExtraBytes; } // Tests/install-time only.
    std::size_t ArenaCount() const { return m_Count; }

private:
    struct Arena { Pool pool {}; };
    class Lock {
    public:
        explicit Lock(SRWLOCK& lock) : m_Lock(lock) { AcquireSRWLockExclusive(&m_Lock); }
        ~Lock() { ReleaseSRWLockExclusive(&m_Lock); }
    private:
        SRWLOCK& m_Lock;
    };
    static bool Valid(const Pool* pool) {
        return pool && pool->begin && pool->capacity && pool->alignment == 32 &&
               reinterpret_cast<std::uintptr_t>(pool->end) -
               reinterpret_cast<std::uintptr_t>(pool->begin) == pool->capacity;
    }
    void Report(GrowthResult result, std::uint32_t request) {
        if (m_Report) m_Report(result, request, m_BaseCapacity + m_ExtraBytes, m_Budget);
    }

    Functions* m_Functions;
    Reporter m_Report;
    ReserveFn m_Reserve;
    ReleaseFn m_Release;
    SRWLOCK m_Lock = SRWLOCK_INIT;
    std::array<Arena, 16> m_Arenas {};
    Pool* m_Owner = nullptr;
    std::uint32_t m_Budget = 1024u * 1024 * 1024;
    std::uint32_t m_BaseCapacity = 0;
    std::uint32_t m_ExtraBytes = 0;
    std::size_t m_Count = 0;
};
}
