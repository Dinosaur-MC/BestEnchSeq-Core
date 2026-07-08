#include "test_utils.h"
#include "utils/SPMCQueue.hpp"
#include <vector>

void test_push_read() {
    SPMCQueue<int, 4> q;
    expect(q.count() == 0, "empty queue count = 0");

    q.push(42);
    expect(q.count() == 1, "after 1 push, count = 1");

    auto cursor = q.read_cursor();
    int val{};
    expect(q.read(cursor, val), "should read pushed value");
    expect(val == 42, "value should be 42");

    // Second read should return false (only 1 item)
    expect(!q.read(cursor, val), "no more items");

    std::cout << "PASS: test_push_read" << std::endl;
}

void test_overflow_drops_oldest() {
    SPMCQueue<int, 4> q;

    // Fill buffer
    q.push(1);
    q.push(2);
    q.push(3);
    q.push(4);
    expect(q.count() == 4, "4 pushes = 4 count");

    // Create cursor BEFORE overflow
    auto old_cursor = q.read_cursor();

    // Push 2 more — should overwrite slots 0 and 1
    q.push(5);
    q.push(6);

    // Old cursor should detect data loss (jumps forward)
    int val{};
    bool ok = q.read(old_cursor, val);
    expect(!ok, "old cursor should detect overwrite and skip");

    // Cursor should have been forwarded; next read should be valid
    ok = q.read(old_cursor, val);
    expect(ok, "after overwrite jump, should get valid data");

    std::cout << "PASS: test_overflow_drops_oldest" << std::endl;
}

void test_read_all_caught_up() {
    SPMCQueue<int, 8> q;

    q.push(10);
    q.push(20);
    q.push(30);

    auto cursor = q.read_cursor();
    int val{};
    expect(q.read(cursor, val) && val == 10, "first item");
    expect(q.read(cursor, val) && val == 20, "second item");
    expect(q.read(cursor, val) && val == 30, "third item");
    expect(!q.read(cursor, val), "no fourth item");

    std::cout << "PASS: test_read_all_caught_up" << std::endl;
}

void test_available() {
    SPMCQueue<int, 8> q;

    expect(q.available(q.read_cursor()) == 0, "empty = 0 avail");

    q.push(1);
    q.push(2);

    auto cursor = q.read_cursor();
    expect(q.available(cursor) == 2, "2 items = 2 avail");

    int val{};
    q.read(cursor, val);
    expect(q.available(cursor) == 1, "1 left after read");

    std::cout << "PASS: test_available" << std::endl;
}

void test_spmc_multiple_consumers() {
    SPMCQueue<int, 16> q;

    for (int i = 0; i < 10; i++)
        q.push(i * 10);

    auto cursor_a = q.read_cursor();
    auto cursor_b = q.read_cursor();

    std::vector<int> a_vals, b_vals;
    int val{};
    while (q.read(cursor_a, val)) a_vals.push_back(val);
    while (q.read(cursor_b, val)) b_vals.push_back(val);

    expect(a_vals.size() == 10, "consumer A reads all");
    expect(b_vals.size() == 10, "consumer B reads all");
    expect(a_vals == b_vals, "both consumers see same data");

    std::cout << "PASS: test_spmc_multiple_consumers" << std::endl;
}

void test_write_after_read() {
    SPMCQueue<int, 4> q;

    q.push(1);
    auto cursor = q.read_cursor();
    int val{};
    q.read(cursor, val); // consume 1

    q.push(2);
    q.push(3);

    expect(q.read(cursor, val) && val == 2, "read second value");
    expect(q.read(cursor, val) && val == 3, "read third value");

    std::cout << "PASS: test_write_after_read" << std::endl;
}

int main() {
    test_push_read();
    test_overflow_drops_oldest();
    test_read_all_caught_up();
    test_available();
    test_spmc_multiple_consumers();
    test_write_after_read();
    std::cout << "All SPMCQueue tests passed!" << std::endl;
    return 0;
}
