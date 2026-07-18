#include "io/ByteStream.h"
#include "framework/test_utils.h"

#include <cstdint>

namespace {

// ===========================================================================
// set_fail() and fail() — explicit failure marking
// ===========================================================================

void test_fresh_reader_has_no_fail() {
    uint8_t buf[] = {1, 2, 3, 4};
    ByteStreamReader r(buf, sizeof(buf));

    expect(r.ok() == true,  "fresh reader: ok() should be true");
    expect(r.fail() == false, "fresh reader: fail() should be false");

    std::cout << "  PASS: test_fresh_reader_has_no_fail" << std::endl;
}

void test_set_fail_marks_reader_failed() {
    uint8_t buf[] = {1, 2, 3};
    ByteStreamReader r(buf, sizeof(buf));

    r.set_fail();

    expect(r.ok() == false,  "after set_fail(): ok() should be false");
    expect(r.fail() == true,  "after set_fail(): fail() should be true");
    expect(r.has_more() == false, "after set_fail(): has_more() should be false");

    std::cout << "  PASS: test_set_fail_marks_reader_failed" << std::endl;
}

void test_reads_return_zero_after_set_fail() {
    uint8_t buf[] = {1, 2, 3, 4, 5, 6, 7, 8};
    ByteStreamReader r(buf, sizeof(buf));

    r.set_fail();

    expect(r.u8() == 0,  "u8 returns 0 after set_fail()");
    expect(r.u16() == 0, "u16 returns 0 after set_fail()");
    expect(r.u32() == 0, "u32 returns 0 after set_fail()");
    expect(r.u64() == 0, "u64 returns 0 after set_fail()");

    std::cout << "  PASS: test_reads_return_zero_after_set_fail" << std::endl;
}

void test_bounds_failure_also_sets_fail() {
    uint8_t buf[] = {1, 2, 3};
    ByteStreamReader r(buf, sizeof(buf));

    // Read beyond bounds
    r.skip(10);
    expect(r.fail() == true,  "bounds failure: fail() should be true");
    expect(r.ok() == false,   "bounds failure: ok() should be false");

    std::cout << "  PASS: test_bounds_failure_also_sets_fail" << std::endl;
}

void test_string_returns_empty_after_set_fail() {
    uint8_t buf[] = {5, 0, 0, 0, 'h', 'e', 'l', 'l', 'o'};
    ByteStreamReader r(buf, sizeof(buf));

    r.set_fail();
    auto s = r.string();
    expect(s.empty(), "string() returns empty after set_fail()");

    std::cout << "  PASS: test_string_returns_empty_after_set_fail" << std::endl;
}

void test_remaining_returns_zero_after_set_fail() {
    uint8_t buf[] = {1, 2, 3, 4};
    ByteStreamReader r(buf, sizeof(buf));

    r.set_fail();
    expect(r.remaining() == 0, "remaining() returns 0 after set_fail()");

    std::cout << "  PASS: test_remaining_returns_zero_after_set_fail" << std::endl;
}

} // anonymous namespace

int main() {
    std::cout << "=== ByteStreamReader set_fail/fail Tests ===" << std::endl;

    try {
        test_fresh_reader_has_no_fail();
        test_set_fail_marks_reader_failed();
        test_reads_return_zero_after_set_fail();
        test_bounds_failure_also_sets_fail();
        test_string_returns_empty_after_set_fail();
        test_remaining_returns_zero_after_set_fail();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }

    return print_summary();
}
