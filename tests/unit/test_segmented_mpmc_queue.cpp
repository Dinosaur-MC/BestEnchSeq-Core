#include "framework/test_utils.h"
#include "utils/SegmentedMPMCQueue.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// ─── Single-threaded basics ───

void test_push_pop() {
    SegmentedMPMCQueue<int, 64> q;
    expect(q.empty(), "empty initially");

    q.push(42);
    expect(!q.empty(), "not empty after push");
    expect(q.size() == 1, "size == 1 after push");

    int val{};
    expect(q.try_pop(val), "pop should succeed");
    expect(val == 42, "value should be 42");
    expect(q.empty(), "empty after pop");
    expect(q.size() == 0, "size == 0 after pop");

    std::cout << "PASS: test_push_pop" << std::endl;
}

void test_empty_pop_returns_false() {
    SegmentedMPMCQueue<int, 64> q;
    int val{};
    expect(!q.try_pop(val), "pop on empty returns false");
    std::cout << "PASS: test_empty_pop_returns_false" << std::endl;
}

void test_fifo_order() {
    constexpr int N = 10000;
    SegmentedMPMCQueue<int, 64> q;

    for (int i = 0; i < N; ++i)
        q.push(i);

    int expected = 0, val{};
    while (q.try_pop(val)) {
        expect(val == expected, "FIFO order violation. Got " +
               std::to_string(val) + " expected " + std::to_string(expected));
        ++expected;
    }
    expect(expected == N, "should pop all N items");
    std::cout << "PASS: test_fifo_order (" << N << " items)" << std::endl;
}

void test_large_push_pop() {
    // Push more than one block's worth of data to trigger block transitions.
    constexpr int N = 100000;
    SegmentedMPMCQueue<int, 256> q;

    for (int i = 0; i < N; ++i)
        q.push(i);

    int expected = 0, val{};
    while (q.try_pop(val)) {
        expect(val == expected, "FIFO order across blocks. Got " +
               std::to_string(val) + " expected " + std::to_string(expected));
        ++expected;
    }
    expect(expected == N, "should pop all N items across blocks");
    std::cout << "PASS: test_large_push_pop (" << N << " items across blocks)" << std::endl;
}

void test_interleaved_push_pop() {
    SegmentedMPMCQueue<int, 64> q;
    for (int i = 0; i < 5000; ++i) {
        q.push(i);
        int val{};
        expect(q.try_pop(val), "pop should succeed");
        expect(val == i, "value should match");
    }
    expect(q.empty(), "queue should be empty after interleaved ops");
    std::cout << "PASS: test_interleaved_push_pop" << std::endl;
}

// ─── Multi-threaded tests ───

void test_two_producers_one_consumer() {
    constexpr int PRODUCE_PER = 50000;
    constexpr int TOTAL = PRODUCE_PER * 2;
    SegmentedMPMCQueue<uint64_t, 256> q;

    std::atomic<bool> done{false};
    std::atomic<uint64_t> consumed{0};
    std::atomic<uint64_t> sum_in{0};
    std::atomic<uint64_t> sum_out{0};

    std::thread p1([&] {
        for (int i = 0; i < PRODUCE_PER; ++i) {
            uint64_t v = static_cast<uint64_t>(i) * 2;
            q.push(v);
            sum_in.fetch_add(v);
        }
    });

    std::thread p2([&] {
        for (int i = 0; i < PRODUCE_PER; ++i) {
            uint64_t v = static_cast<uint64_t>(i) * 2 + 1;
            q.push(v);
            sum_in.fetch_add(v);
        }
    });

    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::now() + std::chrono::seconds(10);

    std::thread consumer([&] {
        uint64_t val{};
        while (consumed.load() < TOTAL && Clock::now() < deadline) {
            if (q.try_pop(val)) {
                consumed.fetch_add(1);
                sum_out.fetch_add(val);
            } else {
                std::this_thread::yield();
            }
        }
    });

    p1.join();
    p2.join();

    // Let consumer drain
    while (consumed.load() < TOTAL && Clock::now() < deadline) {
        std::this_thread::yield();
    }
    done.store(true);

    consumer.join();

    expect(consumed.load() == TOTAL,
           "should consume all items. Got: " + std::to_string(consumed.load()));
    expect(sum_in.load() == sum_out.load(),
           "sum in should equal sum out");
    std::cout << "PASS: test_two_producers_one_consumer ("
              << consumed.load() << "/" << TOTAL << " items)" << std::endl;
}

void test_one_producer_two_consumers() {
    constexpr int N = 100000;
    SegmentedMPMCQueue<int, 256> q;

    std::atomic<bool> done{false};
    std::atomic<int> consumed_a{0}, consumed_b{0};
    std::atomic<int> last_a{-1}, last_b{-1};
    std::atomic<bool> order_ok_a{true}, order_ok_b{true};

    std::thread producer([&] {
        for (int i = 0; i < N; ++i)
            q.push(i);
        done.store(true);
    });

    auto consumer_fn = [&](std::atomic<int>& counter,
                           std::atomic<int>& last,
                           std::atomic<bool>& order_ok) {
        using Clock = std::chrono::steady_clock;
        auto deadline = Clock::now() + std::chrono::seconds(5);
        int val{};
        while (!done.load() || q.size() > 0) {
            if (q.try_pop(val)) {
                counter.fetch_add(1);
                int prev = last.load();
                if (val < prev) {
                    order_ok.store(false);
                }
                last.store(val);
            } else {
                if (Clock::now() >= deadline) break;
                std::this_thread::yield();
            }
        }
    };

    std::thread c1(consumer_fn, std::ref(consumed_a),
                   std::ref(last_a), std::ref(order_ok_a));
    std::thread c2(consumer_fn, std::ref(consumed_b),
                   std::ref(last_b), std::ref(order_ok_b));

    producer.join();
    c1.join();
    c2.join();

    int total = consumed_a.load() + consumed_b.load();
    expect(total == N, "consumers should read all items. Got " +
           std::to_string(total) + "/" + std::to_string(N));
    expect(consumed_a.load() > 0 && consumed_b.load() > 0,
           "both consumers should read items");
    std::cout << "PASS: test_one_producer_two_consumers (A="
              << consumed_a.load() << " B=" << consumed_b.load()
              << " total=" << total << ")" << std::endl;
}

void test_multi_producer_multi_consumer_stress() {
    constexpr int PRODUCERS = 4;
    constexpr int CONSUMERS = 4;
    constexpr int PER_PRODUCER = 25000;
    constexpr int TOTAL = PRODUCERS * PER_PRODUCER;

    SegmentedMPMCQueue<uint64_t, 256> q;

    std::atomic<bool> done{false};
    std::atomic<uint64_t> consumed{0};
    std::atomic<uint64_t> sum_in{0};
    std::atomic<uint64_t> sum_out{0};

    std::vector<std::thread> producers;
    for (int t = 0; t < PRODUCERS; ++t) {
        producers.emplace_back([&, t] {
            for (int i = 0; i < PER_PRODUCER; ++i) {
                uint64_t v = static_cast<uint64_t>(t) * 1000000 + i;
                q.push(v);
                sum_in.fetch_add(v);
            }
        });
    }

    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::now() + std::chrono::seconds(15);

    std::vector<std::thread> consumers;
    for (int t = 0; t < CONSUMERS; ++t) {
        consumers.emplace_back([&] {
            uint64_t val{};
            while (consumed.load() < TOTAL && Clock::now() < deadline) {
                if (q.try_pop(val)) {
                    consumed.fetch_add(1);
                    sum_out.fetch_add(val);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& p : producers) p.join();

    // Drain
    using namespace std::chrono_literals;
    auto drain_deadline = Clock::now() + std::chrono::seconds(5);
    while (consumed.load() < TOTAL && Clock::now() < drain_deadline) {
        std::this_thread::sleep_for(10ms);
    }

    for (auto& c : consumers) c.join();

    expect(consumed.load() == TOTAL,
           "should consume all items. Got " + std::to_string(consumed.load()) +
           "/" + std::to_string(TOTAL));
    expect(sum_in.load() == sum_out.load(),
           "sum in should equal sum out (got " +
           std::to_string(sum_in.load()) + " vs " +
           std::to_string(sum_out.load()) + ")");
    std::cout << "PASS: test_multi_producer_multi_consumer_stress ("
              << consumed.load() << "/" << TOTAL << " items, "
              << PRODUCERS << "P/" << CONSUMERS << "C)" << std::endl;
}

// ─── Non-trivial type ───

struct MoveOnlyStr {
    int id;
    std::string name;

    MoveOnlyStr(int id, std::string name) : id(id), name(std::move(name)) {}
    MoveOnlyStr(MoveOnlyStr&&) = default;
    MoveOnlyStr& operator=(MoveOnlyStr&&) = default;
    ~MoveOnlyStr() = default;
};

void test_move_only_type() {
    SegmentedMPMCQueue<MoveOnlyStr, 64> q;
    q.push(MoveOnlyStr(1, "alice"));
    q.push(MoveOnlyStr(2, "bob"));

    MoveOnlyStr val{0, ""};
    expect(q.try_pop(val), "pop move-only");
    expect(val.id == 1 && val.name == "alice", "first value");
    expect(q.try_pop(val), "pop move-only");
    expect(val.id == 2 && val.name == "bob", "second value");
    expect(!q.try_pop(val), "queue empty");

    std::cout << "PASS: test_move_only_type" << std::endl;
}

int main() {
    std::cout << "=== SegmentedMPMCQueue Tests ===" << std::endl;
    try {
        std::cout << "--- Single-threaded ---" << std::endl;
        test_push_pop();
        test_empty_pop_returns_false();
        test_fifo_order();
        test_large_push_pop();
        test_interleaved_push_pop();
        test_move_only_type();

        std::cout << "--- Multi-threaded ---" << std::endl;
        test_two_producers_one_consumer();
        test_one_producer_two_consumers();
        test_multi_producer_multi_consumer_stress();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
