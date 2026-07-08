#include "test_utils.h"
#include "utils/SPSCQueue.hpp"
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

void test_overflow_drops_oldest() {
    SPSCQueue<int, 4> q;

    q.push(1); q.push(2); q.push(3); q.push(4);
    // Now full. Next push overwrites oldest (1)
    q.push(5);  // overwrites slot 0
    q.push(6);  // overwrites slot 1

    // Should now read 3, 4, 5, 6
    int val{};
    expect(q.pop(val) && val == 3, "first should be 3");
    expect(q.pop(val) && val == 4, "second should be 4");
    expect(q.pop(val) && val == 5, "third should be 5");
    expect(q.pop(val) && val == 6, "fourth should be 6");
    expect(!q.pop(val), "queue should be empty");

    std::cout << "PASS: test_overflow_drops_oldest" << std::endl;
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

int main() {
    std::cout << "=== SPSCQueue Tests ===" << std::endl;
    test_push_pop();
    test_overflow_drops_oldest();
    test_sequential_producer_consumer();
    test_consumer_catch_up();
    test_peek();
    std::cout << "All SPSCQueue tests passed!" << std::endl;
    return 0;
}
