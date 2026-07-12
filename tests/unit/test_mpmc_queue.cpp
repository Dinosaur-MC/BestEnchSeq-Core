#include "framework/test_utils.h"
#include "utils/queue/BoundedMPMCQueue.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

// ─── Single-threaded basics ───

void test_push_pop() {
    BoundedMPMCQueue<int, 4> q;
    expect(q.size() == 0, "empty initially");
    expect(q.empty(), "empty() initially");

    expect(q.try_push(42), "push should succeed");
    expect(!q.empty(), "not empty after push");
    expect(q.size() == 1, "size == 1 after push");

    int val{};
    expect(q.try_pop(val), "pop should succeed");
    expect(val == 42, "value should be 42");
    expect(q.empty(), "empty after pop");
    expect(q.size() == 0, "size == 0 after pop");

    std::cout << "PASS: test_push_pop" << std::endl;
}

void test_drop_on_full() {
    BoundedMPMCQueue<int, 4> q;

    expect(q.try_push(1), "push 1");
    expect(q.try_push(2), "push 2");
    expect(q.try_push(3), "push 3");
    expect(q.try_push(4), "push 4");
    expect(!q.try_push(5), "push 5 should be dropped (full)");
    expect(!q.try_push(6), "push 6 should be dropped (full)");

    int val{};
    expect(q.try_pop(val) && val == 1, "first should be 1");
    expect(q.try_pop(val) && val == 2, "second should be 2");
    expect(q.try_pop(val) && val == 3, "third should be 3");
    expect(q.try_pop(val) && val == 4, "fourth should be 4");
    expect(!q.try_pop(val), "queue should be empty");

    std::cout << "PASS: test_drop_on_full" << std::endl;
}

void test_empty_pop_returns_false() {
    BoundedMPMCQueue<int, 4> q;
    int val{};
    expect(!q.try_pop(val), "pop on empty returns false");
    std::cout << "PASS: test_empty_pop_returns_false" << std::endl;
}

void test_interleaved_push_pop() {
    BoundedMPMCQueue<int, 8> q;
    for (int i = 0; i < 1000; ++i) {
        expect(q.try_push(i), "push should succeed");
        int val{};
        expect(q.try_pop(val), "pop should succeed");
        expect(val == i, "value should match");
    }
    expect(q.empty(), "queue should be empty after interleaved ops");
    std::cout << "PASS: test_interleaved_push_pop" << std::endl;
}

void test_fifo_order() {
    constexpr int N = 1024;  // match capacity exactly
    BoundedMPMCQueue<int, 1024> q;

    for (int i = 0; i < N; ++i)
        q.try_push(i);

    int expected = 0, val{};
    while (q.try_pop(val)) {
        expect(val == expected, "FIFO order violation");
        ++expected;
    }
    expect(expected == N, "should pop all N items");
    std::cout << "PASS: test_fifo_order (" << N << " items)" << std::endl;
}

void test_wrap_around() {
    // Fill, drain, fill, drain multiple times to exercise sequence wrapping
    BoundedMPMCQueue<int, 4> q;

    for (int cycle = 0; cycle < 1000; ++cycle) {
        for (int i = 0; i < 4; ++i)
            expect(q.try_push(i + cycle * 10), "push during wrap test");
        int val{};
        for (int i = 0; i < 4; ++i) {
            expect(q.try_pop(val), "pop during wrap test");
            expect(val == i + cycle * 10, "value during wrap test");
        }
    }
    std::cout << "PASS: test_wrap_around" << std::endl;
}

// ─── Multi-threaded tests ───

void test_two_producers_one_consumer() {
    constexpr int PRODUCE_PER = 50000;
    constexpr int TOTAL = PRODUCE_PER * 2;
    BoundedMPMCQueue<uint64_t, 1024> q;

    std::atomic<bool> done{false};
    std::atomic<uint64_t> consumed{0};
    std::atomic<uint64_t> sum_in{0};
    std::atomic<uint64_t> sum_out{0};

    std::thread p1([&] {
        for (int i = 0; i < PRODUCE_PER; ++i) {
            uint64_t v = static_cast<uint64_t>(i) * 2;
            while (!q.try_push(v)) {}  // spin until slot available
            sum_in.fetch_add(v);
        }
    });

    std::thread p2([&] {
        for (int i = 0; i < PRODUCE_PER; ++i) {
            uint64_t v = static_cast<uint64_t>(i) * 2 + 1;
            while (!q.try_push(v)) {}
            sum_in.fetch_add(v);
        }
    });

    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::now() + std::chrono::seconds(10);

    std::thread consumer([&] {
        uint64_t val{};
        int empty_spins = 0;
        while (consumed.load() < TOTAL && Clock::now() < deadline) {
            if (q.try_pop(val)) {
                consumed.fetch_add(1);
                sum_out.fetch_add(val);
                empty_spins = 0;
            } else {
                ++empty_spins;
                if (empty_spins > 1000)
                    std::this_thread::yield();
            }
        }
    });

    p1.join();
    p2.join();
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
    BoundedMPMCQueue<int, 256> q;

    std::atomic<bool> done{false};
    std::atomic<int> consumed_a{0}, consumed_b{0};

    std::thread producer([&] {
        for (int i = 0; i < N; ++i) {
            while (!q.try_push(i)) {}
        }
        done.store(true);
    });

    auto consumer_fn = [&](std::atomic<int>& counter) {
        using Clock = std::chrono::steady_clock;
        auto deadline = Clock::now() + std::chrono::seconds(5);
        int val{};
        while (!done.load() || q.size() > 0) {
            if (q.try_pop(val)) {
                counter.fetch_add(1);
            } else {
                if (Clock::now() >= deadline) break;
                std::this_thread::yield();
            }
        }
    };

    std::thread c1(consumer_fn, std::ref(consumed_a));
    std::thread c2(consumer_fn, std::ref(consumed_b));

    producer.join();
    c1.join();
    c2.join();

    int total = consumed_a.load() + consumed_b.load();
    expect(total > 0, "consumers should read at least some data");
    expect(consumed_a.load() > 0 && consumed_b.load() > 0,
           "both consumers should read items");
    expect(total <= N, "should not consume more than produced");
    std::cout << "PASS: test_one_producer_two_consumers (A="
              << consumed_a.load() << " B=" << consumed_b.load()
              << " total=" << total << ")" << std::endl;
}

void test_multi_producer_multi_consumer_stress() {
    constexpr int PRODUCERS = 4;
    constexpr int CONSUMERS = 4;
    constexpr int PER_PRODUCER = 25000;
    constexpr int TOTAL = PRODUCERS * PER_PRODUCER;

    BoundedMPMCQueue<uint64_t, 4096> q;

    std::atomic<bool> done{false};
    std::atomic<uint64_t> consumed{0};
    std::atomic<uint64_t> sum_in{0};
    std::atomic<uint64_t> sum_out{0};

    std::vector<std::thread> producers;
    for (int t = 0; t < PRODUCERS; ++t) {
        producers.emplace_back([&, t] {
            for (int i = 0; i < PER_PRODUCER; ++i) {
                uint64_t v = static_cast<uint64_t>(t) * 1000000 + i;
                while (!q.try_push(v)) {}
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
            int empty_spins = 0;
            while (consumed.load() < TOTAL && Clock::now() < deadline) {
                if (q.try_pop(val)) {
                    consumed.fetch_add(1);
                    sum_out.fetch_add(val);
                    empty_spins = 0;
                } else {
                    ++empty_spins;
                    if (empty_spins > 5000)
                        std::this_thread::yield();
                }
            }
        });
    }

    for (auto& p : producers) p.join();

    // Ensure consumers drain remaining items
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(100ms);
    done.store(true);

    for (auto& c : consumers) c.join();

    expect(consumed.load() > 0, "should consume items");
    expect(consumed.load() <= TOTAL, "should not over-consume");
    expect(sum_in.load() == sum_out.load(),
           "sum in should equal sum out (got " +
           std::to_string(sum_in.load()) + " vs " +
           std::to_string(sum_out.load()) + ")");
    std::cout << "PASS: test_multi_producer_multi_consumer_stress ("
              << consumed.load() << "/" << TOTAL << " items, "
              << PRODUCERS << "P/" << CONSUMERS << "C)" << std::endl;
}

// ─── Non-trivial type ───

struct MoveOnly {
    int id;
    std::string name;

    MoveOnly(int id, std::string name) : id(id), name(std::move(name)) {}
    MoveOnly(MoveOnly&&) = default;
    MoveOnly& operator=(MoveOnly&&) = default;
    ~MoveOnly() = default;
};

void test_move_only_type() {
    BoundedMPMCQueue<MoveOnly, 8> q;
    expect(q.try_push(MoveOnly(1, "alice")), "push move-only");
    expect(q.try_push(MoveOnly(2, "bob")), "push move-only");

    MoveOnly val{0, ""};
    expect(q.try_pop(val), "pop move-only");
    expect(val.id == 1 && val.name == "alice", "first value");
    expect(q.try_pop(val), "pop move-only");
    expect(val.id == 2 && val.name == "bob", "second value");
    expect(!q.try_pop(val), "queue empty");

    std::cout << "PASS: test_move_only_type" << std::endl;
}

int main() {
    std::cout << "=== BoundedMPMCQueue Tests ===" << std::endl;
    try {
        std::cout << "--- Single-threaded ---" << std::endl;
        test_push_pop();
        test_drop_on_full();
        test_empty_pop_returns_false();
        test_interleaved_push_pop();
        test_fifo_order();
        test_wrap_around();
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
