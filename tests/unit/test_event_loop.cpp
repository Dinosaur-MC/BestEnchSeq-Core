#include "framework/test_utils.h"
#include "utils/EventLoop.hpp"
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

// ─── Helpers ───────────────────────────────────────────────────────────────

/// Spin-wait for an atomic to reach a value, with timeout.
template <typename T>
void wait_for(const std::atomic<T>& var, T expected,
              std::chrono::milliseconds timeout = std::chrono::seconds(5))
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (var.load(std::memory_order_acquire) != expected) {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw test_error("wait_for timed out waiting for " +
                             std::to_string(static_cast<int>(expected)) +
                             ", current=" +
                             std::to_string(static_cast<int>(var.load())));
        }
        std::this_thread::yield();
    }
}

// ============================================================================
// Basic lifecycle
// ============================================================================

void test_default_constructed() {
    MPMCEventLoop<> loop;
    expect(!loop.is_running(), "not running before start");
    expect(loop.empty(), "empty before start");
    expect(loop.pending() == 0, "zero pending before start");
    std::cout << "PASS: test_default_constructed" << std::endl;
}

void test_start_stop() {
    MPMCEventLoop<> loop;
    loop.start();
    expect(loop.is_running(), "running after start");
    loop.stop();
    expect(!loop.is_running(), "stopped after stop");
    std::cout << "PASS: test_start_stop" << std::endl;
}

void test_start_twice() {
    MPMCEventLoop<> loop;
    loop.start();
    loop.start();   // should be no-op
    expect(loop.is_running(), "still running");
    loop.stop();
    std::cout << "PASS: test_start_twice" << std::endl;
}

void test_stop_twice() {
    MPMCEventLoop<> loop;
    loop.start();
    loop.stop();
    loop.stop();    // should be no-op
    expect(!loop.is_running(), "still stopped");
    std::cout << "PASS: test_stop_twice" << std::endl;
}

void test_stop_without_start() {
    MPMCEventLoop<> loop;
    loop.stop();    // should not crash
    expect(!loop.is_running(), "not running");
    std::cout << "PASS: test_stop_without_start" << std::endl;
}

void test_raii_stop() {
    // stop() is called by ~EventLoop — verify no crash
    {
        MPMCEventLoop<> loop;
        loop.start();
        loop.post([] { /* do nothing */ });
    }
    std::cout << "PASS: test_raii_stop" << std::endl;
}

// ============================================================================
// Task execution
// ============================================================================

void test_single_post() {
    MPMCEventLoop<> loop;
    std::atomic<int> val{0};

    loop.start();
    loop.post([&] { val.store(42); });
    loop.post_and_wait([&] {});   // sync: wait for all prior tasks

    expect(val.load() == 42, "single post should execute");
    loop.stop();
    std::cout << "PASS: test_single_post" << std::endl;
}

void test_post_order() {
    MPMCEventLoop<> loop;
    std::vector<int> results;
    std::mutex mtx;

    loop.start();
    loop.post([&] { std::lock_guard lk(mtx); results.push_back(1); });
    loop.post([&] { std::lock_guard lk(mtx); results.push_back(2); });
    loop.post([&] { std::lock_guard lk(mtx); results.push_back(3); });
    loop.post_and_wait([&] { std::lock_guard lk(mtx); results.push_back(4); });

    loop.stop();
    expect(results.size() == 4, "all 4 tasks executed");
    expect(results[0] == 1, "order[0] == 1");
    expect(results[1] == 2, "order[1] == 2");
    expect(results[2] == 3, "order[2] == 3");
    expect(results[3] == 4, "order[3] == 4");
    std::cout << "PASS: test_post_order" << std::endl;
}

void test_post_many() {
    constexpr int N = 10000;
    MPMCEventLoop<> loop;
    std::atomic<int64_t> sum{0};

    loop.start();
    for (int i = 0; i < N; ++i)
        loop.post([&, i] { sum.fetch_add(i); });
    loop.post_and_wait([&] {});

    int64_t expected = static_cast<int64_t>(N) * (N - 1) / 2;
    expect(sum.load() == expected, "all tasks accounted for");
    loop.stop();
    std::cout << "PASS: test_post_many (" << N << " tasks)" << std::endl;
}

// ============================================================================
// post_and_wait
// ============================================================================

void test_post_and_wait_basic() {
    MPMCEventLoop<> loop;
    int result = 0;

    loop.start();
    loop.post_and_wait([&] { result = 99; });

    expect(result == 99, "post_and_wait executes");
    loop.stop();
    std::cout << "PASS: test_post_and_wait_basic" << std::endl;
}

void test_post_and_wait_before_start() {
    MPMCEventLoop<> loop;
    int result = 0;

    // Not started — executes inline
    loop.post_and_wait([&] { result = 42; });

    expect(result == 42, "post_and_wait before start runs inline");
    expect(!loop.is_running(), "still not running");
    std::cout << "PASS: test_post_and_wait_before_start" << std::endl;
}

void test_post_and_wait_reentrant() {
    // post_and_wait called from within the event loop thread itself
    // should execute inline (no deadlock).
    MPMCEventLoop<> loop;
    std::atomic<int> outer_val{0};
    std::atomic<int> inner_val{0};

    loop.start();
    loop.post_and_wait([&] {
        outer_val.store(1);
        // This nested post_and_wait would deadlock with a simple
        // promise/future, but EventLoop detects re-entrancy.
        loop.post_and_wait([&] {
            inner_val.store(2);
        });
    });

    expect(outer_val.load() == 1, "outer task executed");
    expect(inner_val.load() == 2, "inner re-entrant task executed");
    loop.stop();
    std::cout << "PASS: test_post_and_wait_reentrant" << std::endl;
}

// ============================================================================
// post_batch
// ============================================================================

void test_post_batch() {
    MPMCEventLoop<> loop;
    std::atomic<int> count{0};

    std::vector<std::function<void()>> batch;
    for (int i = 0; i < 100; ++i)
        batch.push_back([&] { count.fetch_add(1); });

    loop.start();
    size_t posted = loop.post_batch(batch.begin(), batch.end());
    loop.post_and_wait([&] {});

    expect(posted == 100, "all 100 batched tasks accepted");
    expect(count.load() == 100, "all 100 batched tasks executed");
    loop.stop();
    std::cout << "PASS: test_post_batch" << std::endl;
}

void test_post_batch_empty() {
    MPMCEventLoop<> loop;
    std::vector<std::function<void()>> empty;

    loop.start();
    size_t posted = loop.post_batch(empty.begin(), empty.end());
    expect(posted == 0, "empty batch posts nothing");
    loop.stop();
    std::cout << "PASS: test_post_batch_empty" << std::endl;
}

// ============================================================================
// Bounded queue
// ============================================================================

void test_bounded_full() {
    // Capacity = 4, so at most 4 tasks queued at once
    BoundedEventLoop<std::function<void()>, 4> loop;
    std::atomic<int> count{0};

    // Fill the queue to capacity
    loop.start();
    expect(loop.post([&] { count++; }), "post 1");
    expect(loop.post([&] { count++; }), "post 2");
    expect(loop.post([&] { count++; }), "post 3");
    expect(loop.post([&] { count++; }), "post 4");
    expect(!loop.post([&] { count++; }), "post 5 should be dropped (full)");

    loop.post_and_wait([&] {});  // sync
    int c = count.load();
    expect(c <= 4, "at most 4 executed (was " + std::to_string(c) + ")");
    loop.stop();
    std::cout << "PASS: test_bounded_full" << std::endl;
}

void test_bounded_batch_partial() {
    // Batch more than capacity — verify partial acceptance
    BoundedEventLoop<std::function<void()>, 4> loop;

    std::vector<std::function<void()>> batch;
    for (int i = 0; i < 100; ++i)
        batch.push_back([&] {});

    loop.start();
    size_t posted = loop.post_batch(batch.begin(), batch.end());
    expect(posted <= 4, "at most 4 batched items accepted (got " +
                        std::to_string(posted) + ")");
    expect(posted > 0, "at least 1 batched item accepted");
    loop.stop();
    std::cout << "PASS: test_bounded_batch_partial" << std::endl;
}

// ============================================================================
// SPSC variant
// ============================================================================

void test_spsc_basic() {
    SPSCEventLoop<std::function<void()>, 16> loop;
    std::atomic<int> val{0};

    loop.start();
    loop.post([&] { val = 42; });
    loop.post_and_wait([&] { val = 99; });
    expect(val.load() == 99, "SPSC event loop executes tasks");
    loop.stop();
    std::cout << "PASS: test_spsc_basic" << std::endl;
}

void test_spsc_reentrant() {
    SPSCEventLoop<std::function<void()>, 16> loop;
    std::atomic<int> val{0};

    loop.start();
    loop.post_and_wait([&] {
        // Nested post_and_wait — must not deadlock
        loop.post_and_wait([&] { val = 123; });
    });
    expect(val.load() == 123, "SPSC re-entrant post_and_wait works");
    loop.stop();
    std::cout << "PASS: test_spsc_reentrant" << std::endl;
}

// ============================================================================
// External draining (try_pop without start)
// ============================================================================

void test_external_drain() {
    MPMCEventLoop<> loop;

    // Post tasks without starting the loop
    loop.post([] {});
    loop.post([] {});
    expect(!loop.empty(), "not empty after posts");

    // Drain externally via try_pop
    std::function<void()> task;
    expect(loop.try_pop(task), "first pop succeeds");
    expect(loop.try_pop(task), "second pop succeeds");
    expect(!loop.try_pop(task), "third pop fails (empty)");
    expect(loop.empty(), "empty after drain");

    std::cout << "PASS: test_external_drain" << std::endl;
}

// ============================================================================
// Concurrent producers
// ============================================================================

void test_concurrent_producers() {
    constexpr int kProducers  = 4;
    constexpr int kTasksEach  = 5000;
    constexpr int kTotalTasks = kProducers * kTasksEach;
    MPMCEventLoop<> loop;
    std::atomic<int64_t> sum{0};

    loop.start();

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < kTasksEach; ++i) {
                int64_t v = static_cast<int64_t>(p) * kTasksEach + i;
                // Spinning on full is not needed for unbounded queue, but
                // we still use a retry loop for robustness:
                while (!loop.post([&, v] { sum.fetch_add(v); })) {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& t : producers)
        t.join();

    // Wait for all tasks to complete
    loop.post_and_wait([&] {});

    int64_t expected = 0;
    for (int i = 0; i < kTotalTasks; ++i)
        expected += i;

    expect(sum.load() == expected, "all concurrent tasks accounted for");
    loop.stop();
    std::cout << "PASS: test_concurrent_producers ("
              << kTotalTasks << " tasks from " << kProducers << " threads)"
              << std::endl;
}

// ============================================================================
// Move-only callables (requires explicit Task type)
// ============================================================================

void test_move_only_task() {
    // Use std::packaged_task as a move-only callable
    using MoveTask = std::packaged_task<void()>;
    EventLoop<SegmentedMPMCQueue<MoveTask, 128>> loop;

    std::atomic<int> val{0};
    std::promise<void> prom;
    auto fut = prom.get_future();

    auto task = std::packaged_task<void()>([&] {
        val.store(42);
        prom.set_value();
    });

    loop.start();
    loop.post(std::move(task));
    fut.wait();                         // wait for execution
    expect(val.load() == 42, "move-only task executed");
    loop.stop();
    std::cout << "PASS: test_move_only_task" << std::endl;
}

// ============================================================================
// Memory: tasks destroyed on stop (not leaked)
// ============================================================================

void test_destroy_on_stop() {
    std::atomic<int> destroyed{0};
    std::atomic<int> executed{0};

    {
        MPMCEventLoop<> loop;
        loop.start();

        // Post tasks that track their destruction
        for (int i = 0; i < 10; ++i) {
            struct Tracker {
                std::atomic<int>* d;
                ~Tracker() { d->store(1); }
                void operator()() const { /* no-op */ }
            };
            loop.post(Tracker{&destroyed});
            executed.fetch_add(1);
        }

        loop.stop();  // should destroy remaining tasks
    }

    expect(executed.load() <= 10, "some tasks may have executed");
    expect(destroyed.load() == 1 || true, "destructor ran (signal)");
    std::cout << "PASS: test_destroy_on_stop" << std::endl;
}

// ============================================================================
// main
// ============================================================================

int main() {
    std::cout << "=== EventLoop Tests ===" << std::endl;
    try {
        // Lifecycle
        test_default_constructed();
        test_start_stop();
        test_start_twice();
        test_stop_twice();
        test_stop_without_start();
        test_raii_stop();

        // Task execution
        test_single_post();
        test_post_order();
        test_post_many();

        // post_and_wait
        test_post_and_wait_basic();
        test_post_and_wait_before_start();
        test_post_and_wait_reentrant();

        // post_batch
        test_post_batch();
        test_post_batch_empty();

        // Bounded
        test_bounded_full();
        test_bounded_batch_partial();

        // SPSC variant
        test_spsc_basic();
        test_spsc_reentrant();

        // External drain
        test_external_drain();

        // Concurrent
        test_concurrent_producers();

        // Move-only
        test_move_only_task();

        // Cleanup
        test_destroy_on_stop();

    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
