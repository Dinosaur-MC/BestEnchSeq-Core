// SPDX-License-Identifier: MIT
//
// ThreadPool test suite
// =====================
// Covers: basic submit/wait, future results, exception propagation,
// parallel_for, drain-on-destruction, move-only callables, concurrent
// submission from multiple threads, and edge cases (empty pool, large
// batches).

#include "framework/test_utils.h"
#include "utils/thread/ThreadPool.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

using namespace besq;
namespace chrono = std::chrono;

// ============================================================================
// Helpers
// ============================================================================

/// Spin-wait (with timeout) until a predicate becomes true.
template <typename Pred>
void spin_wait(Pred&& pred,
               chrono::milliseconds timeout = chrono::seconds(10)) {
    auto deadline = chrono::steady_clock::now() + timeout;
    while (!pred()) {
        if (chrono::steady_clock::now() >= deadline) {
            throw test_error("spin_wait timed out");
        }
        std::this_thread::yield();
    }
}

// ============================================================================
// 1. Basic lifecycle
// ============================================================================

void test_default_constructed() {
    ThreadPool pool(1);
    expect(pool.size() == 1, "pool should have 1 worker");
    expect(pool.pending() == 0, "no pending tasks initially");
    TEST_PASS("test_default_constructed");
}

void test_multi_worker_count() {
    ThreadPool pool(4);
    expect(pool.size() == 4, "pool should have 4 workers");
    TEST_PASS("test_multi_worker_count");
}

void test_zero_threads_auto() {
    ThreadPool pool(0);
    expect(pool.size() > 0, "pool with 0 should auto-detect");
    TEST_PASS("test_zero_threads_auto");
}

void test_mode_default() {
    ThreadPool pool(2);
    expect(pool.mode() == ThreadPoolMode::SingleQueue,
           "default mode should be SingleQueue");
    TEST_PASS("test_mode_default");
}

// ============================================================================
// 2. Submit with simple tasks
// ============================================================================

void test_submit_void() {
    ThreadPool pool(2);
    std::atomic<int> counter{0};

    auto fut = pool.submit([&counter] { counter.fetch_add(1); });
    fut.get(); // wait for completion

    expect(counter.load() == 1, "counter should be 1 after one task");
    TEST_PASS("test_submit_void");
}

void test_submit_return_value() {
    ThreadPool pool(2);

    auto fut = pool.submit([] { return 42; });
    int result = fut.get();

    expect(result == 42, "should return 42");
    TEST_PASS("test_submit_return_value");
}

void test_submit_string() {
    ThreadPool pool(2);

    auto fut = pool.submit([] {
        return std::string("hello from thread");
    });
    auto result = fut.get();

    expect(result == "hello from thread", "should return correct string");
    TEST_PASS("test_submit_string");
}

void test_submit_multiple_tasks() {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    constexpr int N = 100;

    std::vector<std::future<void>> futures;
    futures.reserve(N);
    for (int i = 0; i < N; ++i) {
        futures.push_back(pool.submit([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }
    for (auto& f : futures) f.get();

    expect(counter.load() == N, "all tasks should have run");
    TEST_PASS("test_submit_multiple_tasks");
}

// ============================================================================
// 3. wait()
// ============================================================================

void test_wait_basic() {
    ThreadPool pool(2);
    std::atomic<int> counter{0};

    for (int i = 0; i < 50; ++i) {
        pool.submit([&counter] {
            std::this_thread::sleep_for(chrono::milliseconds(1));
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    pool.wait();
    expect(counter.load() == 50, "all tasks should have completed after wait");
    TEST_PASS("test_wait_basic");
}

// ============================================================================
// 4. parallel_for
// ============================================================================

void test_parallel_for_basic() {
    ThreadPool pool(4);
    constexpr std::size_t N = 10'000;
    std::vector<int> data(N, 0);

    parallel_for(pool, std::size_t{0}, N, [&](std::size_t i) {
        data[i] = static_cast<int>(i);
    });

    bool ok = true;
    for (std::size_t i = 0; i < N && ok; ++i) {
        if (data[i] != static_cast<int>(i)) ok = false;
    }
    expect(ok, "parallel_for should fill all elements correctly");
    TEST_PASS("test_parallel_for_basic");
}

void test_parallel_for_empty_range() {
    ThreadPool pool(2);
    // Should not crash or hang
    parallel_for(pool, std::size_t{0}, std::size_t{0},
                 [&](std::size_t) {});
    TEST_PASS("test_parallel_for_empty_range");
}

void test_parallel_for_single_element() {
    ThreadPool pool(2);
    int value = 0;

    parallel_for(pool, 0, 1, [&](int i) { value = i + 1; });

    expect(value == 1, "single element should be processed");
    TEST_PASS("test_parallel_for_single_element");
}

void test_parallel_for_exception() {
    ThreadPool pool(2);

    bool threw = false;
    try {
        parallel_for(pool, std::size_t{0}, std::size_t{100},
                     [&](std::size_t i) {
            if (i == 42) throw std::runtime_error("test error");
        });
    } catch (const std::runtime_error& e) {
        threw = true;
        expect(std::string(e.what()) == "test error",
               "exception message should be preserved");
    }
    expect(threw, "parallel_for should propagate exception");
    TEST_PASS("test_parallel_for_exception");
}

void test_parallel_for_large_batch() {
    ThreadPool pool(4);
    constexpr std::size_t N = 100'000;
    std::vector<uint64_t> data(N);

    parallel_for(pool, std::size_t{0}, N, [&](std::size_t i) {
        data[i] = static_cast<uint64_t>(i) * i;
    });

    uint64_t checksum = 0;
    for (auto v : data) checksum += v;

    // Sum of i^2 for i=0..N-1 = (N-1)*N*(2N-1)/6
    uint64_t expected = (N - 1) * static_cast<uint64_t>(N) * (2 * N - 1) / 6;
    expect(checksum == expected, "parallel_for large batch checksum should match");
    TEST_PASS("test_parallel_for_large_batch");
}

void test_parallel_for_worker_count_respected() {
    // With 1 worker, tasks run sequentially (still correct)
    ThreadPool pool(1);
    constexpr std::size_t N = 1000;
    std::vector<int> data(N, 0);

    parallel_for(pool, std::size_t{0}, N, [&](std::size_t i) {
        data[i] = 1;
    });

    int sum = std::accumulate(data.begin(), data.end(), 0);
    expect(sum == static_cast<int>(N), "single-worker parallel_for should still work");
    TEST_PASS("test_parallel_for_worker_count_respected");
}

void test_parallel_for_custom_chunk_size() {
    ThreadPool pool(4);
    constexpr std::size_t N = 1000;
    std::vector<int> data(N, 0);

    // Use a large explicit chunk size (all in one task)
    parallel_for(pool, std::size_t{0}, N, [&](std::size_t i) {
        data[i] = 1;
    }, N);

    int sum = 0;
    for (auto v : data) sum += v;
    expect(sum == static_cast<int>(N), "custom chunk size should work");
    TEST_PASS("test_parallel_for_custom_chunk_size");
}

// ============================================================================
// 5. Exception propagation
// ============================================================================

void test_exception_propagation() {
    ThreadPool pool(2);

    auto fut = pool.submit([]() -> int {
        throw std::runtime_error("boom");
        return 0;
    });

    bool caught = false;
    try {
        fut.get();
    } catch (const std::runtime_error& e) {
        caught = true;
        expect(std::string(e.what()) == "boom",
               "exception message should be preserved");
    }
    expect(caught, "exception should propagate through future");
    TEST_PASS("test_exception_propagation");
}

// ============================================================================
// 6. Move-only callable
// ============================================================================

void test_move_only_callable() {
    ThreadPool pool(2);

    // A move-only type
    struct MoveOnly {
        int value;
        MoveOnly(int v) : value(v) {}
        MoveOnly(const MoveOnly&) = delete;
        MoveOnly(MoveOnly&&) = default;
        int operator()() const { return value; }
    };

    auto fut = pool.submit(MoveOnly(99));
    int result = fut.get();

    expect(result == 99, "move-only callable should work");
    TEST_PASS("test_move_only_callable");
}

// ============================================================================
// 7. Drain on stop / destruction
// ============================================================================

void test_drain_on_stop() {
    std::atomic<int> counter{0};
    constexpr int N = 50;

    {
        ThreadPool pool(4);
        for (int i = 0; i < N; ++i) {
            pool.submit([&counter] {
                std::this_thread::sleep_for(chrono::milliseconds(5));
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }
        // pool destructor will drain and join
    }

    expect(counter.load() == N,
           "all tasks should complete on pool destruction");
    TEST_PASS("test_drain_on_stop");
}

void test_explicit_stop_then_submit_throws() {
    ThreadPool pool(2);
    pool.stop();

    bool caught = false;
    try {
        auto fut = pool.submit([] {});
        fut.get();
    } catch (const std::exception&) {
        caught = true;
    }
    expect(caught, "submit after stop should throw (or future should error)");
    TEST_PASS("test_explicit_stop_then_submit_throws");
}

// ============================================================================
// 8. Concurrent submission from multiple threads
// ============================================================================

void test_concurrent_submission() {
    ThreadPool pool(4);
    constexpr int kProducers = 4;
    constexpr int kTasksPerProducer = 250;
    std::atomic<int> counter{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&pool, &counter] {
            for (int i = 0; i < kTasksPerProducer; ++i) {
                pool.submit([&counter] {
                    counter.fetch_add(1, std::memory_order_relaxed);
                });
            }
        });
    }
    for (auto& t : producers) t.join();

    pool.wait();
    expect(counter.load() == kProducers * kTasksPerProducer,
           "all concurrently-submitted tasks should complete");
    TEST_PASS("test_concurrent_submission");
}

// ============================================================================
// 9. Large batch of fine-grained tasks
// ============================================================================

void test_large_batch() {
    ThreadPool pool(4);
    constexpr int N = 10'000;
    std::atomic<int64_t> sum{0};

    for (int i = 0; i < N; ++i) {
        pool.submit([&sum, i] {
            sum.fetch_add(i, std::memory_order_relaxed);
        });
    }

    pool.wait();

    int64_t expected = static_cast<int64_t>(N) * (N - 1) / 2;
    expect(sum.load() == expected,
           "large batch sum should match");
    TEST_PASS("test_large_batch");
}

// ============================================================================
// 10. Shared pool singleton
// ============================================================================

void test_shared_pool_exists() {
    auto& pool = ThreadPool::shared();
    expect(pool.size() > 0, "shared pool should have workers");
    TEST_PASS("test_shared_pool_exists");
}

// ============================================================================
// 11. Stress: many short tasks
// ============================================================================

void test_many_short_tasks() {
    ThreadPool pool(std::min(8u, std::thread::hardware_concurrency()));
    constexpr int N = 50'000;
    std::atomic<int64_t> sum{0};

    for (int i = 0; i < N; ++i) {
        pool.submit([&sum] {
            sum.fetch_add(1, std::memory_order_relaxed);
        });
    }

    pool.wait();
    expect(sum.load() == N, "all short tasks should complete");
    TEST_PASS("test_many_short_tasks");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    try {
        // 1. Basic lifecycle
        test_default_constructed();
        test_multi_worker_count();
        test_zero_threads_auto();
        test_mode_default();

        // 2. Submit with simple tasks
        test_submit_void();
        test_submit_return_value();
        test_submit_string();
        test_submit_multiple_tasks();

        // 3. wait()
        test_wait_basic();

        // 4. parallel_for
        test_parallel_for_basic();
        test_parallel_for_empty_range();
        test_parallel_for_single_element();
        test_parallel_for_exception();
        test_parallel_for_large_batch();
        test_parallel_for_worker_count_respected();
        test_parallel_for_custom_chunk_size();

        // 5. Exception propagation
        test_exception_propagation();

        // 6. Move-only callable
        test_move_only_callable();

        // 7. Drain on stop / destruction
        test_drain_on_stop();
        test_explicit_stop_then_submit_throws();

        // 8. Concurrent submission
        test_concurrent_submission();

        // 9. Large batch
        test_large_batch();

        // 10. Shared pool
        test_shared_pool_exists();

        // 11. Stress
        test_many_short_tasks();

    } catch (const test_error& e) {
        std::cerr << "FAIL: " << e.what() << std::endl;
        return print_summary();
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }

    return print_summary();
}
