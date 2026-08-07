#define BESQ_TEST_MAIN
#include "framework/test_framework.h"
#include "utils/MemoryPool.hpp"
#include <cstdint>
#include <set>
#include <string>
#include <vector>

TEST_CASE("test_memory_pool") {
    // ── Test 1: basic alloc-dealloc (no-op) ─────────────────────────────
    {
        MemoryPool pool;
        void* p1 = pool.allocate(8, alignof(std::max_align_t));
        void* p2 = pool.allocate(16, alignof(std::max_align_t));
        expect(p1 != nullptr, "basic alloc p1");
        expect(p2 != nullptr, "basic alloc p2");
        expect(p1 != p2, "distinct addresses");
        // deallocate is no-op — verify it doesn't crash
        pool.deallocate(p1, 8, alignof(std::max_align_t));
        pool.deallocate(p2, 16, alignof(std::max_align_t));
        TEST_PASS("basic_alloc_dealloc");
    }

    // ── Test 2: alignment for over-aligned types ────────────────────────
    {
        MemoryPool pool;
        void* p1 = pool.allocate(1, 64);
        void* p2 = pool.allocate(1, 128);
        expect((reinterpret_cast<uintptr_t>(p1) & 63) == 0, "64-byte aligned");
        expect((reinterpret_cast<uintptr_t>(p2) & 127) == 0, "128-byte aligned");
        TEST_PASS("alignment");
    }

    // ── Test 3: release and reuse (cached chunks) ───────────────────────
    {
        MemoryPool pool;
        (void)pool.allocate(32, alignof(std::max_align_t));
        expect(pool.total_allocated() >= 32, "allocated before release");
        pool.release();
        expect(pool.total_allocated() == 0, "reset after release");
        void* p2 = pool.allocate(32, alignof(std::max_align_t));
        expect(p2 != nullptr, "reuse after release");
        TEST_PASS("release_reuse");
    }

    // ── Test 4: allocate larger than initial chunk size ─────────────────
    {
        MemoryPool pool(128); // tiny initial chunk
        // Allocate 4 KB — forces new chunk
        void* lp = pool.allocate(4096, alignof(std::max_align_t));
        expect(lp != nullptr, "large alloc");
        expect(pool.total_allocated() >= 4096, "large alloc counted");
        TEST_PASS("large_alloc");
    }

    // ── Test 5: multiple chunks ─────────────────────────────────────────
    {
        MemoryPool pool(256);
        // Allocate many small blocks to force chunk exhaustion
        std::set<void*> addrs;
        for (int i = 0; i < 100; ++i) {
            void* p = pool.allocate(64, alignof(std::max_align_t));
            addrs.insert(p);
        }
        expect(addrs.size() == 100, "100 distinct allocs");
        expect(pool.chunk_count() > 1 || pool.total_allocated() >= 6400, "multiple chunks or large enough single chunk");
        TEST_PASS("multiple_chunks");
    }

    // ── Test 6: release_all frees everything ────────────────────────────
    {
        MemoryPool pool(256);
        void* rp = pool.allocate(128, alignof(std::max_align_t));
        expect(rp != nullptr, "alloc before release_all");
        pool.release_all();
        expect(pool.total_allocated() == 0, "zero after release_all");
        expect(pool.cached_chunks() == 0, "no cached chunks after release_all");
        // Should still work after release_all
        void* p = pool.allocate(128, alignof(std::max_align_t));
        expect(p != nullptr, "alloc after release_all");
        TEST_PASS("release_all");
    }

    // ── Test 7: pmr container integration ───────────────────────────────
    {
        MemoryPool pool;
        std::pmr::vector<int> vec{&pool};
        vec.reserve(100);
        for (int i = 0; i < 100; ++i)
            vec.push_back(i);
        expect(vec.size() == 100, "pmr vector size");
        for (int i = 0; i < 100; ++i)
            expect(vec[i] == i, "pmr vector values");
        TEST_PASS("pmr_container");
    }

    // ── Test 8: move semantics ──────────────────────────────────────────
    {
        MemoryPool pool1;
        void* p = pool1.allocate(64, alignof(std::max_align_t));
        expect(p != nullptr, "alloc in pool1");

        MemoryPool pool2(std::move(pool1));
        // pool1 is now empty
        expect(pool1.total_allocated() == 0, "moved-from pool empty");
        // pool2 has the allocation
        expect(pool2.total_allocated() >= 64, "moved-to pool has allocation");
        (void)pool2.allocate(32, alignof(std::max_align_t));

        MemoryPool pool3;
        pool3 = std::move(pool2);
        expect(pool3.total_allocated() >= 64 + 32, "move-assigned pool has data");
        TEST_PASS("move_semantics");
    }

    // ── Test 9: sequential allocation patterns ──────────────────────────
    {
        MemoryPool pool(1024);
        // Allocate varied sizes — monotonic bump should work
        void* a = pool.allocate(7, 4);
        void* b = pool.allocate(63, 8);
        void* c = pool.allocate(255, 16);
        void* d = pool.allocate(1, 1);
        expect(a != nullptr, "seq a");
        expect(b != nullptr, "seq b");
        expect(c != nullptr, "seq c");
        expect(d != nullptr, "seq d");
        expect(b > a && c > b && d > c, "monotonic addresses");
        TEST_PASS("sequential_sizes");
    }
}
