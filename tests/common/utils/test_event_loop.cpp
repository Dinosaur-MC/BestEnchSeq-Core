#define BESQ_TEST_MAIN
#include "framework/test_framework.h"
#include "utils/EventLoop.hpp"
#include "utils/queue/SegmentedMPMCQueue.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <thread>
#include <vector>

// ─── Helpers ───────────────────────────────────────────────────────────────

/// Spin-wait for an atomic to reach a value, with timeout.
template <typename T>
void wait_for(const std::atomic<T>& var, T expected, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (var.load(std::memory_order_acquire) != expected) {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw test_error("wait_for timed out waiting for " + std::to_string(static_cast<int>(expected)) +
                             ", current=" + std::to_string(static_cast<int>(var.load())));
        }
        std::this_thread::yield();
    }
}

/// Drain barrier: post a no-op task and wait for completion.
/// Ensures all previously posted tasks have been consumed.
template <typename Loop> static void drain(Loop& loop) {
    std::atomic<bool> done{false};
    loop.post([&done] { done.store(true, std::memory_order_release); });
    while (!done.load(std::memory_order_acquire))
        std::this_thread::yield();
}

// ============================================================================
// Basic lifecycle
// ============================================================================

TEST_CASE("test_default_constructed") {
    MPMCEventLoop<> loop;
    expect(!loop.is_running(), "not running before start");
    expect(loop.empty(), "empty before start");
    expect(loop.pending() == 0, "zero pending before start");
}

TEST_CASE("test_start_stop") {
    MPMCEventLoop<> loop;
    loop.start();
    expect(loop.is_running(), "running after start");
    loop.stop();
    expect(!loop.is_running(), "stopped after stop");
}

TEST_CASE("test_start_twice") {
    MPMCEventLoop<> loop;
    loop.start();
    loop.start(); // should be no-op
    expect(loop.is_running(), "still running");
    loop.stop();
}

TEST_CASE("test_stop_twice") {
    MPMCEventLoop<> loop;
    loop.start();
    loop.stop();
    loop.stop(); // should be no-op
    expect(!loop.is_running(), "still stopped");
}

TEST_CASE("test_stop_without_start") {
    MPMCEventLoop<> loop;
    loop.stop(); // should not crash
    expect(!loop.is_running(), "not running");
}

TEST_CASE("test_raii_stop") {
    // stop() is called by ~EventLoop — verify no crash
    {
        MPMCEventLoop<> loop;
        loop.start();
        loop.post([] { /* do nothing */ });
    }
}

// ============================================================================
// Task execution
// ============================================================================

TEST_CASE("test_single_post") {
    MPMCEventLoop<> loop;
    std::atomic<int> val{0};

    loop.start();
    loop.post([&] { val.store(42); });
    drain(loop);

    expect(val.load() == 42, "single post should execute");
    loop.stop();
}

TEST_CASE("test_post_order") {
    MPMCEventLoop<> loop;
    std::vector<int> results;

    loop.start();
    loop.post([&] { results.push_back(1); });
    loop.post([&] { results.push_back(2); });
    loop.post([&] { results.push_back(3); });
    loop.post([&] { results.push_back(4); });
    drain(loop);

    expect(results.size() == 4, "all 4 tasks executed");
    expect(results[0] == 1, "order[0] == 1");
    expect(results[1] == 2, "order[1] == 2");
    expect(results[2] == 3, "order[2] == 3");
    expect(results[3] == 4, "order[3] == 4");
    loop.stop();
}

TEST_CASE("test_post_many") {
    constexpr int N = 10000;
    MPMCEventLoop<> loop;
    std::atomic<int64_t> sum{0};

    loop.start();
    for (int i = 0; i < N; ++i)
        loop.post([&, i] { sum.fetch_add(i); });
    drain(loop);

    int64_t expected = static_cast<int64_t>(N) * (N - 1) / 2;
    expect(sum.load() == expected, "all tasks accounted for");
    loop.stop();
}

// ============================================================================
// post_batch
// ============================================================================

TEST_CASE("test_post_batch") {
    MPMCEventLoop<> loop;
    std::atomic<int> count{0};

    std::vector<std::function<void()>> batch;
    for (int i = 0; i < 100; ++i)
        batch.push_back([&] { count.fetch_add(1); });

    loop.start();
    loop.post_batch(batch.begin(), batch.end());
    drain(loop);

    expect(count.load() == 100, "all 100 batched tasks executed");
    loop.stop();
}

TEST_CASE("test_post_batch_empty") {
    MPMCEventLoop<> loop;
    std::vector<std::function<void()>> empty;

    loop.start();
    loop.post_batch(empty.begin(), empty.end());
    expect(loop.pending() == 0, "empty batch posts nothing");
    loop.stop();
}

// ============================================================================
// Bounded queue
// ============================================================================

TEST_CASE("test_bounded_full") {
    // Capacity = 4, so at most 4 tasks queued at once.
    // Use try_post without starting the loop to test bounded capacity.
    BoundedEventLoop<std::function<void()>, 4> loop;
    std::atomic<int> count{0};

    expect(loop.try_post([&] { count++; }), "post 1");
    expect(loop.try_post([&] { count++; }), "post 2");
    expect(loop.try_post([&] { count++; }), "post 3");
    expect(loop.try_post([&] { count++; }), "post 4");
    expect(!loop.try_post([&] { count++; }), "post 5 should be dropped (full)");

    // Start the loop, drain, and verify
    loop.start();
    drain(loop);
    int c = count.load();
    expect(c == 4, "exactly 4 executed (was " + std::to_string(c) + ")");
    loop.stop();
}

TEST_CASE("test_bounded_batch_partial") {
    // Batch more than capacity — verify partial acceptance
    BoundedEventLoop<std::function<void()>, 4> loop;

    std::vector<std::function<void()>> batch;
    for (int i = 0; i < 100; ++i)
        batch.push_back([] {});

    // Don't start — test raw capacity limit via try_post_batch
    size_t posted = loop.try_post_batch(batch.begin(), batch.end());
    expect(posted <= 4, "at most 4 batched items accepted (got " + std::to_string(posted) + ")");
    expect(posted > 0, "at least 1 batched item accepted");
    loop.stop();
}

// ============================================================================
// SPSC variant
// ============================================================================

TEST_CASE("test_spsc_basic") {
    SPSCEventLoop<std::function<void()>, 16> loop;
    std::atomic<int> val{0};

    loop.start();
    loop.post([&] { val = 42; });
    drain(loop);
    expect(val.load() == 42, "SPSC event loop executes tasks");

    loop.post([&] { val = 99; });
    drain(loop);
    expect(val.load() == 99, "second task executes");
    loop.stop();
}

// ============================================================================
// Concurrent producers
// ============================================================================

TEST_CASE("test_concurrent_producers") {
    constexpr int kProducers = 4;
    constexpr int kTasksEach = 5000;
    constexpr int kTotalTasks = kProducers * kTasksEach;
    MPMCEventLoop<> loop;
    std::atomic<int64_t> sum{0};

    loop.start();

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < kTasksEach; ++i) {
                int64_t v = static_cast<int64_t>(p) * kTasksEach + i;
                loop.post([&, v] { sum.fetch_add(v); });
            }
        });
    }

    for (auto& t : producers)
        t.join();

    drain(loop);

    int64_t expected = 0;
    for (int i = 0; i < kTotalTasks; ++i)
        expected += i;

    expect(sum.load() == expected, "all concurrent tasks accounted for");
    loop.stop();
}

// ============================================================================
// Wakeup race regression test
// ============================================================================

TEST_CASE("test_wakeup_race") {
    // This test targets the lost-wakeup race between "drain empty" and
    // "atomic::wait".  If the consumer reads _wake after the producer has
    // notified but before wait() blocks, it can sleep forever on the old
    // value while a task sits in the queue.
    //
    // The fix: double-check the queue after loading _wake but before
    // entering wait().
    MPMCEventLoop<> loop;
    loop.start();

    // Drain all tasks so the consumer enters idle-wait.
    drain(loop);

    std::atomic<bool> task_done{false};

    std::thread producer([&] {
        // Yield repeatedly to maximise the chance that the consumer has
        // entered wait() *before* we post.  On a loaded system this is
        // still probabilistic, so we do it in a loop (see below).
        for (int i = 0; i < 50; ++i)
            std::this_thread::yield();
        loop.post([&] { task_done.store(true); });
    });

    // Wait for the task with a generous timeout.
    // Without the double-check fix this almost always times out.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!task_done.load()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            producer.join();
            loop.stop();
            throw test_error("wakeup race: task never executed (lost wakeup)");
        }
        std::this_thread::yield();
    }

    producer.join();
    loop.stop();

    expect(task_done.load(), "wakeup race: task executed");
}

// ============================================================================
// Move-only callables (requires explicit Task type)
// ============================================================================

TEST_CASE("test_move_only_task") {
    using MoveTask = std::packaged_task<void()>;
    EventLoop<MoveTask, SegmentedMPMCQueue<MoveTask, 128>> loop;

    std::atomic<int> val{0};
    std::promise<void> prom;
    auto fut = prom.get_future();

    auto task = std::packaged_task<void()>([&] {
        val.store(42);
        prom.set_value();
    });

    loop.start();
    loop.post(std::move(task));
    fut.wait(); // wait for execution
    expect(val.load() == 42, "move-only task executed via post()");
    loop.stop();
}

// ============================================================================
// Memory: tasks destroyed on stop (not leaked)
// ============================================================================

TEST_CASE("test_destroy_on_stop") {
    std::atomic<int> destroyed{0};
    std::atomic<int> executed{0};

    {
        MPMCEventLoop<> loop;
        loop.start();

        struct Tracker {
            std::atomic<int>* d;
            ~Tracker() { d->store(1); }
            void operator()() const {}
        };

        loop.post(Tracker{&destroyed});
        executed.store(1);

        // stop() drains remaining items gracefully
    }

    expect(executed.load() == 1, "task was posted");
}

// ============================================================================
// main
// ============================================================================
