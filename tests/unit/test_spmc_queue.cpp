#include "framework/test_utils.h"
#include "utils/SPMCQueue.hpp"
#include <atomic>
#include <thread>
#include <vector>

// ─── Single-threaded basics ───

void test_push_read() {
    SPMCQueue<int, 4> q;
    expect(q.count() == 0, "empty queue count = 0");
    q.push(42);
    expect(q.count() == 1, "after 1 push, count = 1");

    auto cursor = q.read_cursor();
    int val{};
    expect(q.read(cursor, val), "should read pushed value");
    expect(val == 42, "value should be 42");
    expect(!q.read(cursor, val), "no more items");
    std::cout << "PASS: test_push_read" << std::endl;
}

void test_overflow_drops_oldest() {
    SPMCQueue<int, 4> q;
    q.push(1); q.push(2); q.push(3); q.push(4);
    expect(q.count() == 4, "4 pushes = 4 count");

    auto old_cursor = q.read_cursor();
    q.push(5); q.push(6);  // overwrites slots 0, 1

    int val{};
    expect(!q.read(old_cursor, val), "old cursor should skip overwritten data");
    expect(q.read(old_cursor, val), "after jump, should get valid data");
    std::cout << "PASS: test_overflow_drops_oldest" << std::endl;
}

void test_read_all_caught_up() {
    SPMCQueue<int, 8> q;
    q.push(10); q.push(20); q.push(30);
    auto cursor = q.read_cursor();
    int val{};
    expect(q.read(cursor, val) && val == 10, "first item");
    expect(q.read(cursor, val) && val == 20, "second item");
    expect(q.read(cursor, val) && val == 30, "third item");
    expect(!q.read(cursor, val), "no fourth item");
    std::cout << "PASS: test_read_all_caught_up" << std::endl;
}

void test_multiple_cursors() {
    SPMCQueue<int, 16> q;
    for (int i = 0; i < 10; i++) q.push(i * 10);

    auto ca = q.read_cursor(), cb = q.read_cursor();
    std::vector<int> a, b;
    int val{};
    while (q.read(ca, val)) a.push_back(val);
    while (q.read(cb, val)) b.push_back(val);
    expect(a.size() == 10 && b.size() == 10, "both cursors read all");
    expect(a == b, "both cursors see same data");
    std::cout << "PASS: test_multiple_cursors" << std::endl;
}

// ─── Multi-threaded tests ───

void test_producer_then_consumer() {
    constexpr int N = 100000;
    SPMCQueue<int, 64> q;

    // Single-threaded: produce then consume
    for (int i = 0; i < N; i++)
        q.push(i);

    auto cursor = q.read_cursor();
    int last = -1, count = 0;
    int val{};
    while (q.read(cursor, val)) {
        if (val <= last) {
            std::cerr << "FAIL: order broken at count=" << count
                      << " last=" << last << " val=" << val << std::endl;
            expect(false, "SPMC: values should be in order");
        }
        last = val;
        count++;
    }
    std::cout << "  (read " << count << " values from " << N << ")" << std::endl;
    std::cout << "PASS: test_producer_then_consumer (" << N << " items)" << std::endl;
}

void test_spmc_two_consumers() {
    constexpr int N = 50000;
    SPMCQueue<int, 256> q;

    std::thread prod([&] {
        for (int i = 0; i < N; i++)
            q.push(i);
    });

    std::atomic<bool> done{false};
    std::atomic<int> consumed_a{0}, consumed_b{0};

    auto consumer_fn = [&](std::atomic<int>& counter) {
        auto cursor = q.read_cursor();
        int idle = 0;
        int val{};
        while (!done.load() || q.available(cursor) > 0) {
            if (q.read(cursor, val)) {
                counter.fetch_add(1);
                idle = 0;
            } else {
                if (++idle > 1000000) break;  // timeout
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
    expect(consumed_a.load() > 0 && consumed_b.load() > 0,
           "both consumers should read items");
    std::cout << "PASS: test_spmc_two_consumers (A="
              << consumed_a.load() << " B=" << consumed_b.load()
              << " total=" << total << ")" << std::endl;
}

void test_overflow_with_producer_lead() {
    // Producer runs far ahead, consumer lags — forces overwrite
    SPMCQueue<int, 16> q;

    // Push 32 items (2 full cycles), consumer hasn't started
    for (int i = 0; i < 32; i++)
        q.push(i);

    // Consumer starts late — should detect overwrite and get recent data
    auto cursor = q.read_cursor();
    int val{};
    // First read should fail (cursor jumps forward)
    bool first = q.read(cursor, val);
    // After the jump, remaining reads should succeed
    int count = 0;
    while (q.read(cursor, val)) {
        expect(val >= 16 && val < 32, "only recent values survive overwrite");
        count++;
    }
    expect(count > 0, "should recover some values after overflow");
    std::cout << "PASS: test_overflow_with_producer_lead (" << count << " items)" << std::endl;
}

int main() {
    std::cout << "=== Single-threaded ===" << std::endl;
    try {
        test_push_read();
        test_overflow_drops_oldest();
        test_read_all_caught_up();
        test_multiple_cursors();

        std::cout << "=== Multi-threaded ===" << std::endl;
        test_producer_then_consumer();
        test_spmc_two_consumers();
        test_overflow_with_producer_lead();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
