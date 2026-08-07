#define BESQ_TEST_MAIN
#include "framework/test_framework.h"
#include "utils/queue/SegmentedMPSCQueue.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

// ─── Single-threaded basics ───

TEST_CASE("test_push_pop") {
    SegmentedMPSCQueue<int> q;
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

TEST_CASE("test_empty_pop_returns_false") {
    SegmentedMPSCQueue<int> q;
    int val{};
    expect(!q.try_pop(val), "pop on empty returns false");
    std::cout << "PASS: test_empty_pop_returns_false" << std::endl;
}

TEST_CASE("test_fifo_order") {
    constexpr int N = 10000;
    SegmentedMPSCQueue<int> q;

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

TEST_CASE("test_interleaved_push_pop") {
    SegmentedMPSCQueue<int> q;
    for (int i = 0; i < 1000; ++i) {
        expect(q.try_push(i), "push should succeed");
        int val{};
        expect(q.try_pop(val), "pop should succeed");
        expect(val == i, "value should match");
    }
    expect(q.empty(), "queue should be empty after interleaved ops");
    std::cout << "PASS: test_interleaved_push_pop" << std::endl;
}

TEST_CASE("test_clear") {
    SegmentedMPSCQueue<int> q;
    for (int i = 0; i < 100; ++i)
        q.try_push(i);

    expect(!q.empty(), "not empty before clear");
    expect(q.size() == 100, "size == 100 before clear");

    q.clear();
    expect(q.empty(), "empty after clear");
    expect(q.size() == 0, "size == 0 after clear");

    // Queue should still be usable after clear
    expect(q.try_push(42), "push after clear should succeed");
    int val{};
    expect(q.try_pop(val), "pop after clear should succeed");
    expect(val == 42, "value after clear should match");

    std::cout << "PASS: test_clear" << std::endl;
}

TEST_CASE("test_capacity_is_zero") {
    SegmentedMPSCQueue<int> q;
    expect(q.capacity() == 0, "unbounded queue reports capacity == 0");
    std::cout << "PASS: test_capacity_is_zero" << std::endl;
}

// ─── Non-trivial type ───

struct MoveOnlyPayload {
    int id;
    std::string name;

    MoveOnlyPayload(int id, std::string name) : id(id), name(std::move(name)) {}
    MoveOnlyPayload(MoveOnlyPayload&&) = default;
    MoveOnlyPayload& operator=(MoveOnlyPayload&&) = default;
    ~MoveOnlyPayload() = default;
};

TEST_CASE("test_move_only_type") {
    SegmentedMPSCQueue<MoveOnlyPayload> q;
    expect(q.try_push(MoveOnlyPayload(1, "alice")), "push move-only");
    expect(q.try_push(MoveOnlyPayload(2, "bob")), "push move-only");

    MoveOnlyPayload val{0, ""};
    expect(q.try_pop(val), "pop move-only");
    expect(val.id == 1 && val.name == "alice", "first value");
    expect(q.try_pop(val), "pop move-only");
    expect(val.id == 2 && val.name == "bob", "second value");
    expect(!q.try_pop(val), "queue empty");

    std::cout << "PASS: test_move_only_type" << std::endl;
}

struct NonDefaultConstructible {
    int value;
    explicit NonDefaultConstructible(int v) : value(v) {}
    NonDefaultConstructible() = delete;
    NonDefaultConstructible(NonDefaultConstructible&&) = default;
    NonDefaultConstructible& operator=(NonDefaultConstructible&&) = default;
    ~NonDefaultConstructible() = default;
};

TEST_CASE("test_non_default_constructible") {
    SegmentedMPSCQueue<NonDefaultConstructible> q;
    expect(q.try_push(NonDefaultConstructible(42)), "push nDefaultConstructible");
    expect(q.try_push(NonDefaultConstructible(99)), "push nDefaultConstructible");

    NonDefaultConstructible val{0};
    expect(q.try_pop(val), "pop nDefaultConstructible");
    expect(val.value == 42, "first value");
    expect(q.try_pop(val), "pop nDefaultConstructible");
    expect(val.value == 99, "second value");
    expect(!q.try_pop(val), "queue empty");

    std::cout << "PASS: test_non_default_constructible" << std::endl;
}

TEST_CASE("test_clear_non_default_constructible") {
    SegmentedMPSCQueue<NonDefaultConstructible> q;
    q.try_push(NonDefaultConstructible(1));
    q.try_push(NonDefaultConstructible(2));
    q.try_push(NonDefaultConstructible(3));

    q.clear();
    expect(q.empty(), "empty after clear");
    expect(q.size() == 0, "size == 0 after clear");

    // Still usable
    expect(q.try_push(NonDefaultConstructible(4)), "push after clear");
    NonDefaultConstructible val{0};
    expect(q.try_pop(val) && val.value == 4, "pop after clear");

    std::cout << "PASS: test_clear_non_default_constructible" << std::endl;
}

// ─── Multi-threaded tests ───

TEST_CASE("test_two_producers_one_consumer") {
    constexpr int PRODUCE_PER = 100000;
    constexpr int TOTAL = PRODUCE_PER * 2;
    SegmentedMPSCQueue<uint64_t> q;

    std::atomic<uint64_t> consumed{0};
    std::atomic<uint64_t> sum_in{0};
    std::atomic<uint64_t> sum_out{0};

    std::thread p1([&] {
        for (int i = 0; i < PRODUCE_PER; ++i) {
            uint64_t v = static_cast<uint64_t>(i) * 2;
            q.try_push(v);
            sum_in.fetch_add(v);
        }
    });

    std::thread p2([&] {
        for (int i = 0; i < PRODUCE_PER; ++i) {
            uint64_t v = static_cast<uint64_t>(i) * 2 + 1;
            q.try_push(v);
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
        // Drain any remaining items (producers may have finished)
        while (q.try_pop(val)) {
            consumed.fetch_add(1);
            sum_out.fetch_add(val);
        }
    });

    p1.join();
    p2.join();
    consumer.join();

    expect(consumed.load() == TOTAL, "should consume all items. Got: " + std::to_string(consumed.load()));
    expect(sum_in.load() == sum_out.load(), "sum in should equal sum out");
    std::cout << "PASS: test_two_producers_one_consumer (" << consumed.load() << "/" << TOTAL << " items)" << std::endl;
}

TEST_CASE("test_multi_producer_stress") {
    constexpr int PRODUCERS = 4;
    constexpr int PER_PRODUCER = 50000;
    constexpr int TOTAL = PRODUCERS * PER_PRODUCER;

    SegmentedMPSCQueue<uint64_t> q;

    std::atomic<uint64_t> consumed{0};
    std::atomic<uint64_t> sum_in{0};
    std::atomic<uint64_t> sum_out{0};
    std::atomic<bool> producers_done{false};

    std::vector<std::thread> producers;
    for (int t = 0; t < PRODUCERS; ++t) {
        producers.emplace_back([&, t] {
            for (int i = 0; i < PER_PRODUCER; ++i) {
                uint64_t v = static_cast<uint64_t>(t) * 1000000 + i;
                q.try_push(v);
                sum_in.fetch_add(v);
            }
        });
    }

    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::now() + std::chrono::seconds(15);

    std::thread consumer([&] {
        uint64_t val{};
        while (Clock::now() < deadline) {
            if (q.try_pop(val)) {
                consumed.fetch_add(1);
                sum_out.fetch_add(val);
            } else if (producers_done.load() && q.empty()) {
                break;
            } else {
                std::this_thread::yield();
            }
        }
        // Final drain
        while (q.try_pop(val)) {
            consumed.fetch_add(1);
            sum_out.fetch_add(val);
        }
    });

    for (auto& p : producers)
        p.join();
    producers_done.store(true);

    consumer.join();

    expect(consumed.load() == TOTAL,
           "should consume all items. Got: " + std::to_string(consumed.load()) + " / " + std::to_string(TOTAL));
    expect(sum_in.load() == sum_out.load(), "sum in should equal sum out");
    std::cout << "PASS: test_multi_producer_stress (" << consumed.load() << "/" << TOTAL << " items, " << PRODUCERS
              << " producers)" << std::endl;
}

TEST_CASE("test_sequence_uniqueness") {
    // Verify no duplicates across 4 producers
    constexpr int PRODUCERS = 4;
    constexpr int PER_PRODUCER = 25000;
    constexpr int TOTAL = PRODUCERS * PER_PRODUCER;

    SegmentedMPSCQueue<uint64_t> q;
    std::atomic<uint64_t> consumed{0};
    std::atomic<bool> producers_done{false};

    std::vector<std::thread> producers;
    for (int t = 0; t < PRODUCERS; ++t) {
        producers.emplace_back([&, t] {
            for (int i = 0; i < PER_PRODUCER; ++i) {
                // Each producer writes globally unique values
                uint64_t v = static_cast<uint64_t>(t) * PER_PRODUCER + i;
                q.try_push(v);
            }
        });
    }

    std::unordered_set<uint64_t> seen;
    std::mutex seen_mutex;

    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::now() + std::chrono::seconds(10);

    std::thread consumer([&] {
        uint64_t val{};
        while (Clock::now() < deadline) {
            if (q.try_pop(val)) {
                {
                    std::lock_guard<std::mutex> lock(seen_mutex);
                    expect(seen.find(val) == seen.end(), "duplicate value detected: " + std::to_string(val));
                    seen.insert(val);
                }
                consumed.fetch_add(1);
            } else if (producers_done.load() && q.empty()) {
                break;
            } else {
                std::this_thread::yield();
            }
        }
        // Final drain
        while (q.try_pop(val)) {
            {
                std::lock_guard<std::mutex> lock(seen_mutex);
                expect(seen.find(val) == seen.end(), "duplicate value detected: " + std::to_string(val));
                seen.insert(val);
            }
            consumed.fetch_add(1);
        }
    });

    for (auto& p : producers)
        p.join();
    producers_done.store(true);

    consumer.join();

    expect(consumed.load() == TOTAL, "should consume all unique items. Got: " + std::to_string(consumed.load()));
    expect(seen.size() == static_cast<size_t>(TOTAL), "set size should match total. Got: " + std::to_string(seen.size()));
    std::cout << "PASS: test_sequence_uniqueness (" << seen.size() << "/" << TOTAL << " unique)" << std::endl;
}

// ─── QueueType concept check ───

TEST_CASE("test_queue_type_concept") {
    static_assert(QueueType<SegmentedMPSCQueue<int>, int>, "SegmentedMPSCQueue<int> must satisfy QueueType<int>");
    static_assert(QueueType<SegmentedMPSCQueue<std::string>, std::string>,
                  "SegmentedMPSCQueue<string> must satisfy QueueType<string>");

    // Move-only type
    static_assert(QueueType<SegmentedMPSCQueue<std::unique_ptr<int>>, std::unique_ptr<int>>,
                  "SegmentedMPSCQueue<unique_ptr> must satisfy QueueType");

    std::cout << "PASS: test_queue_type_concept" << std::endl;
}

// ─── QueueAdaptor integration ───

TEST_CASE("test_queue_adaptor_integration") {
    QueueAdaptor<int, SegmentedMPSCQueue<int>> adaptor;
    IQueue<int>& iq = adaptor;

    expect(iq.capacity() == 0, "capacity via IQueue");
    expect(iq.empty(), "empty via IQueue");

    iq.try_push(42);
    expect(!iq.empty(), "not empty after push via IQueue");

    int val{};
    expect(iq.try_pop(val), "pop via IQueue");
    expect(val == 42, "value via IQueue");

    // Check underlying access
    auto& underlying = adaptor.underlying();
    underlying.try_push(99);
    expect(adaptor.try_pop(val), "pop via adaptor after underlying push");
    expect(val == 99, "value from underlying push");

    std::cout << "PASS: test_queue_adaptor_integration" << std::endl;
}
