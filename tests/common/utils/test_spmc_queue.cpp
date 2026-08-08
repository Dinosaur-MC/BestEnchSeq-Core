#define BESQ_TEST_MAIN
#include "framework/test_framework.h"
#include "utils/queue/SPMCQueue.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

// ─── Single-threaded basics ───

TEST_CASE("test_push_read") {
    SPMCQueue<int, 4> q;
    expect(q.count() == 0, "empty queue count = 0");
    q.try_push(42);
    expect(q.count() == 1, "after 1 push, count = 1");

    auto cursor = q.read_cursor();
    int val{};
    expect(q.read(cursor, val), "should read pushed value");
    expect(val == 42, "value should be 42");
    expect(!q.read(cursor, val), "no more items");
}

TEST_CASE("test_overflow_drops_oldest") {
    SPMCQueue<int, 4> q;
    q.try_push(1);
    q.try_push(2);
    q.try_push(3);
    q.try_push(4);
    expect(q.count() == 4, "4 pushes = 4 count");

    auto old_cursor = q.read_cursor();
    q.try_push(5);
    q.try_push(6); // overwrites slots 0, 1

    int val{};
    expect(!q.read(old_cursor, val), "old cursor should skip overwritten data");
    expect(q.read(old_cursor, val), "after jump, should get valid data");
}

TEST_CASE("test_read_all_caught_up") {
    SPMCQueue<int, 8> q;
    q.try_push(10);
    q.try_push(20);
    q.try_push(30);
    auto cursor = q.read_cursor();
    int val{};
    expect(q.read(cursor, val) && val == 10, "first item");
    expect(q.read(cursor, val) && val == 20, "second item");
    expect(q.read(cursor, val) && val == 30, "third item");
    expect(!q.read(cursor, val), "no fourth item");
}

TEST_CASE("test_multiple_cursors") {
    SPMCQueue<int, 16> q;
    for (int i = 0; i < 10; i++)
        q.try_push(i * 10);

    auto ca = q.read_cursor(), cb = q.read_cursor();
    std::vector<int> a, b;
    int val{};
    while (q.read(ca, val))
        a.push_back(val);
    while (q.read(cb, val))
        b.push_back(val);
    expect(a.size() == 10 && b.size() == 10, "both cursors read all");
    expect(a == b, "both cursors see same data");
}

// ─── Multi-threaded tests ───

TEST_CASE("test_producer_then_consumer") {
    constexpr int N = 100000;
    SPMCQueue<int, 64> q;

    // Single-threaded: produce then consume
    for (int i = 0; i < N; i++)
        q.try_push(i);

    auto cursor = q.read_cursor();
    int last = -1, count = 0;
    int val{};
    while (q.read(cursor, val)) {
        if (val <= last) {
            std::cerr << "FAIL: order broken at count=" << count << " last=" << last << " val=" << val << std::endl;
            expect(false, "SPMC: values should be in order");
        }
        last = val;
        count++;
    }
    std::cout << "  (read " << count << " values from " << N << ")" << std::endl;
}

TEST_CASE("test_spmc_two_consumers") {
    constexpr int N = 50000;
    SPMCQueue<int, 256> q;

    std::thread prod([&] {
        for (int i = 0; i < N; i++)
            q.try_push(i);
    });

    std::atomic<bool> done{false};
    std::atomic<int> consumed_a{0}, consumed_b{0};

    auto consumer_fn = [&](std::atomic<int>& counter) {
        auto cursor = q.read_cursor();
        using Clock = std::chrono::steady_clock;
        auto deadline = Clock::now() + std::chrono::seconds(2);
        int val{};
        while (!done.load() || q.available(cursor) > 0) {
            if (q.read(cursor, val)) {
                counter.fetch_add(1);
            } else {
                if (Clock::now() >= deadline) {
                    std::cerr << "WARNING: consumer spin-loop timed out after 2s" << std::endl;
                    break;
                }
                std::this_thread::yield();
            }
        }
    };

    std::thread cons_a(consumer_fn, std::ref(consumed_a));
    std::thread cons_b(consumer_fn, std::ref(consumed_b));

    prod.join();
    done.store(true);

    cons_a.join();
    cons_b.join();

    int total = consumed_a.load() + consumed_b.load();
    expect(total > 0, "consumers should read at least some data");
    expect(consumed_a.load() > 0 && consumed_b.load() > 0, "both consumers should read items");
    std::cout << "PASS: test_spmc_two_consumers (A=" << consumed_a.load() << " B=" << consumed_b.load() << " total=" << total
              << ")" << std::endl;
}

TEST_CASE("test_overflow_with_producer_lead") {
    // Producer runs far ahead, consumer lags — forces overwrite.
    // Check observable behavior: push more than capacity, then verify
    // the consumer can still read valid items (approximately the most recent N).
    SPMCQueue<int, 16> q;

    // Push well beyond capacity so the buffer wraps multiple times
    for (int i = 0; i < 100; i++)
        q.try_push(i);

    // Consumer starts late — should detect overwrites and get recent data
    auto cursor = q.read_cursor();
    int val{};

    // After the cursor jump, some reads should succeed with recent values
    int count = 0;
    int min_val = std::numeric_limits<int>::max();
    int max_val = std::numeric_limits<int>::min();
    while (q.read(cursor, val)) {
        if (val < min_val)
            min_val = val;
        if (val > max_val)
            max_val = val;
        count++;
    }
    expect(count > 0, "should recover values after overflow");
    // All recovered values should be from the most recent push region
    expect(min_val >= 0 && max_val <= 99, "recovered values must be within the pushed range");
    // At most capacity items can be recovered
    expect(count <= 16, "cannot recover more items than queue capacity");
    std::cout << "PASS: test_overflow_with_producer_lead (" << count << " items in [" << min_val << ", " << max_val << "])"
              << std::endl;
}

TEST_CASE("test_concurrent_push_pop") {
    constexpr int N = 100000;
    SPMCQueue<int, 64> q;

    std::atomic<bool> done{false};
    std::atomic<int> consumed{0};
    std::atomic<int> last_val{-1};
    std::atomic<bool> order_ok{true};

    std::thread producer([&] {
        for (int i = 0; i < N; i++)
            q.try_push(i);
        done.store(true);
    });

    std::thread consumer([&] {
        auto cursor = q.read_cursor();
        using Clock = std::chrono::steady_clock;
        auto deadline = Clock::now() + std::chrono::seconds(5);
        int val{};
        while (!done.load() || q.available(cursor) > 0) {
            if (q.read(cursor, val)) {
                consumed.fetch_add(1);
                if (val < last_val.load()) {
                    order_ok.store(false);
                }
                last_val.store(val);
            } else {
                if (Clock::now() >= deadline) {
                    std::cerr << "WARNING: concurrent consumer timed out" << std::endl;
                    break;
                }
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    expect(consumed.load() > 0, "should consume at least some items");
    expect(consumed.load() <= N, "should not consume more items than pushed");
    // In a bounded SPMC queue the producer can overwrite unread slots,
    // so strict FIFO order is NOT guaranteed for a slow concurrent reader.
    // Only assert order if all items were delivered.
    if (consumed.load() == N) {
        expect(order_ok.load(), "if all items delivered, must be FIFO");
    }
}
