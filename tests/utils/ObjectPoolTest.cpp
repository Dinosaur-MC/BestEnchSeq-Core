#include "framework/test_utils.h"
#include "utils/ObjectPool.hpp"
#include <string>
#include <utility>
#include <vector>

struct Point {
    float x, y, z;
    int lifetime;
    Point(float x_, float y_, float z_, int lt = 100)
        : x(x_), y(y_), z(z_), lifetime(lt) {}
};

struct ThrowOnConstruct {
    static bool should_throw;
    ThrowOnConstruct() {
        if (should_throw) throw std::runtime_error("ctor fail");
    }
    ~ThrowOnConstruct() = default;
};
bool ThrowOnConstruct::should_throw = false;

int main() {
    // ── Test 1: basic acquire/release ───────────────────────────────────
    {
        ObjectPool<Point> pool;
        Point* p = pool.acquire(1.0f, 2.0f, 3.0f);
        expect(p != nullptr, "acquire non-null");
        expect_eq(p->x, 1.0f, "x value");
        expect_eq(p->y, 2.0f, "y value");
        expect_eq(p->z, 3.0f, "z value");
        expect_eq(p->lifetime, 100, "default lifetime");
        pool.release(p);
        TEST_PASS("acquire_release");
    }

    // ── Test 2: address reuse after release ─────────────────────────────
    {
        ObjectPool<Point> pool;
        Point* p1 = pool.acquire(0.0f, 0.0f, 0.0f);
        pool.release(p1);
        Point* p2 = pool.acquire(1.0f, 2.0f, 3.0f);
        expect(p1 == p2, "reuse same address");
        TEST_PASS("reuse_address");
    }

    // ── Test 3: acquire beyond one block ────────────────────────────────
    {
        ObjectPool<int> pool(2);  // 2^2 = 4, then 8, 16...
        std::vector<int*> ptrs;
        for (int i = 0; i < 20; ++i) {
            int* p = pool.acquire(i);
            ptrs.push_back(p);
        }
        expect(pool.capacity() >= 20, "capacity grows");
        for (int i = 0; i < 20; ++i)
            expect_eq(*ptrs[i], i, "values preserved");
        TEST_PASS("block_growth");
    }

    // ── Test 4: clear frees all blocks ──────────────────────────────────
    {
        ObjectPool<int> pool;
        for (int i = 0; i < 100; ++i) (void)pool.acquire(i);
        expect(pool.capacity() > 0, "non-zero before clear");
        pool.clear();
        expect(pool.capacity() == 0, "zero after clear");
        expect(pool.available() == 0, "zero available after clear");
        TEST_PASS("clear");
    }

    // ── Test 5: release all, then re-acquire ────────────────────────────
    {
        ObjectPool<int> pool;
        std::vector<int*> ptrs;
        for (int i = 0; i < 50; ++i) ptrs.push_back(pool.acquire(i));
        for (auto* p : ptrs) pool.release(p);
        expect(pool.available() == pool.capacity(), "all freed");
        // Re-acquire
        for (int i = 0; i < 50; ++i) {
            int* p = pool.acquire(i);
            expect(p != nullptr, "re-acquire");
            expect_eq(*p, i, "re-acquired value");
        }
        TEST_PASS("release_all_then_acquire");
    }

    // ── Test 6: exception safety ────────────────────────────────────────
    {
        ObjectPool<ThrowOnConstruct> pool;
        ThrowOnConstruct::should_throw = true;
        try {
            (void)pool.acquire();
            expect(false, "should have thrown");
        } catch (const std::runtime_error&) {
            expect(pool.available() == pool.capacity(),
                   "node returned to free list after exception");
        }
        ThrowOnConstruct::should_throw = false;
        // Pool should still be usable
        ThrowOnConstruct* obj = pool.acquire();
        expect(obj != nullptr, "acquire works after exception");
        pool.release(obj);
        TEST_PASS("exception_safety");
    }

    // ── Test 7: move semantics ──────────────────────────────────────────
    {
        ObjectPool<int> pool1;
        (void)pool1.acquire(42);
        ObjectPool<int> pool2(std::move(pool1));
        expect(pool1.capacity() == 0, "moved-from empty");
        expect(pool2.capacity() > 0, "moved-to has capacity");
        TEST_PASS("move_semantics");
    }

    // ── Test 8: non-default-constructible type ──────────────────────────
    {
        ObjectPool<std::pair<int, std::string>> pool;
        auto* p = pool.acquire(1, "hello");
        expect_eq(p->first, 1, "pair first");
        expect_eq(p->second, "hello", "pair second");
        pool.release(p);
        TEST_PASS("non_default_constructible");
    }

    return print_summary();
}
