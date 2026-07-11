#include "framework/test_utils.h"
#include "utils/SPSCQueue.hpp"
#include <atomic>
#include <chrono>
#include <thread>

void test_push_pop() {
    SPSCQueue<int, 4> q;
    expect(q.empty(), "empty initially");
    expect(q.size() == 0, "size 0 initially");

    q.push(42);
    expect(!q.empty(), "not empty after push");

    int val{};
    expect(q.pop(val), "should pop pushed value");
    expect(val == 42, "value should be 42");
    expect(q.empty(), "empty after pop");
    expect(!q.pop(val), "pop on empty returns false");

    std::cout << "PASS: test_push_pop" << std::endl;
}

void test_drop_on_full() {
    SPSCQueue<int, 4> q;

    // Fill queue
    expect(q.push(1), "push 1");
    expect(q.push(2), "push 2");
    expect(q.push(3), "push 3");
    expect(q.push(4), "push 4");

    // Full — next pushes should return false (drop newest)
    expect(!q.push(5), "push 5 should be dropped (full)");
    expect(!q.push(6), "push 6 should be dropped (full)");

    // Read back only the first 4 values
    int val{};
    expect(q.pop(val) && val == 1, "first should be 1");
    expect(q.pop(val) && val == 2, "second should be 2");
    expect(q.pop(val) && val == 3, "third should be 3");
    expect(q.pop(val) && val == 4, "fourth should be 4");
    expect(!q.pop(val), "queue should be empty");

    std::cout << "PASS: test_drop_on_full" << std::endl;
}

void test_sequential_producer_consumer() {
    constexpr int N = 100000;
    SPSCQueue<int, 64> q;

    for (int i = 0; i < N; i++)
        q.push(i);

    int last = -1, count = 0, val{};
    while (q.pop(val)) {
        expect(val > last, "SPSC: values should be in order");
        last = val;
        count++;
    }
    std::cout << "  (read " << count << " values from " << N << ")" << std::endl;
    std::cout << "PASS: test_sequential_producer_consumer" << std::endl;
}

void test_consumer_catch_up() {
    // Producer finishes, then consumer reads all — matches real usage
    constexpr int N = 100000;
    SPSCQueue<int, 256> q;

    std::thread prod([&] {
        for (int i = 0; i < N; i++)
            q.push(i);
    });
    prod.join();

    int last = -1, count = 0, val{};
    while (q.pop(val)) {
        expect(val > last, "SPSC: values in order after producer done");
        last = val;
        count++;
    }
    std::cout << "  (read " << count << " values from " << N << ")" << std::endl;
    std::cout << "PASS: test_consumer_catch_up" << std::endl;
}

void test_peek() {
    SPSCQueue<int, 8> q;

    int val{};
    expect(!q.peek(val), "peek on empty returns false");

    q.push(42);
    expect(q.peek(val), "peek should succeed");
    expect(val == 42, "peek value should be 42");
    expect(q.size() == 1, "size unchanged after peek");

    q.pop(val);
    expect(!q.peek(val), "peek after pop returns false");

    std::cout << "PASS: test_peek" << std::endl;
}

void test_concurrent_push_pop() {
    // Concurrent producer and consumer — verify no lost/duplicated items
    // and exact FIFO ordering.
    constexpr int N = 100000;
    SPSCQueue<int, 64> q;

    std::atomic<bool> done{false};
    std::atomic<int> consumed{0};
    std::atomic<int> last_val{-1};
    std::atomic<bool> order_ok{true};

    std::thread producer([&] {
        for (int i = 0; i < N; i++) {
            while (!q.push(i)) {
                // Queue full — yield and retry
                std::this_thread::yield();
            }
        }
        done.store(true);
    });

    std::thread consumer([&] {
        using Clock = std::chrono::steady_clock;
        auto deadline = Clock::now() + std::chrono::seconds(5);
        int val{};
        while (!done.load() || q.size() > 0) {
            if (q.pop(val)) {
                consumed.fetch_add(1);
                if (val < last_val.load()) {
                    order_ok.store(false);
                }
                last_val.store(val);
            } else {
                if (Clock::now() >= deadline) {
                    std::cerr << "WARNING: SPSC concurrent consumer timed out"
                              << std::endl;
                    break;
                }
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    expect(consumed.load() == N, "SPSC: should consume exactly all pushed items");
    expect(order_ok.load(), "SPSC: items should be in FIFO order");
    std::cout << "PASS: test_concurrent_push_pop (consumed "
              << consumed.load() << "/" << N << " items)" << std::endl;
}

int main() {
    std::cout << "=== SPSCQueue Tests ===" << std::endl;
    try {
        test_push_pop();
        test_drop_on_full();
        test_sequential_producer_consumer();
        test_consumer_catch_up();
        test_peek();
        test_concurrent_push_pop();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
