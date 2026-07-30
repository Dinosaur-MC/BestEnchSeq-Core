#include "framework/test_utils.h"
#include "common/utils/bit_iterator.hpp"

#include <cstdint>
#include <limits>

namespace {

// ===========================================================================
// bit_iterator — next()-based iteration
// ===========================================================================

void test_bit_iterator_empty() {
    bit_iterator<uint64_t, uint8_t> it(uint64_t{0});
    expect_eq(it.next(), it.npos, "empty iterator returns npos immediately");
    expect_eq(it.next(), it.npos, "second next() on empty still returns npos");

    std::cout << "  PASS: test_bit_iterator_empty" << std::endl;
}

void test_bit_iterator_single_bit_low() {
    // Bit 0 set
    bit_iterator<uint64_t, uint8_t> it(uint64_t{1});
    expect_eq(it.next(), uint8_t{0}, "bit 0 -> index 0");
    expect_eq(it.next(), it.npos, "exhausted -> npos");

    std::cout << "  PASS: test_bit_iterator_single_bit_low" << std::endl;
}

void test_bit_iterator_single_bit_high() {
    // Bit 63 set (MSB of uint64_t)
    bit_iterator<uint64_t, uint8_t> it(uint64_t{1} << 63);
    expect_eq(it.next(), uint8_t{63}, "bit 63 -> index 63");
    expect_eq(it.next(), it.npos, "exhausted -> npos");

    std::cout << "  PASS: test_bit_iterator_single_bit_high" << std::endl;
}

void test_bit_iterator_single_bit_mid() {
    // Bit 31 set
    bit_iterator<uint64_t, uint8_t> it(uint64_t{1} << 31);
    expect_eq(it.next(), uint8_t{31}, "bit 31 -> index 31");
    expect_eq(it.next(), it.npos, "exhausted -> npos");

    std::cout << "  PASS: test_bit_iterator_single_bit_mid" << std::endl;
}

void test_bit_iterator_consecutive_bits() {
    // 4 LSBs set: 0b1111
    bit_iterator<uint64_t, uint8_t> it(uint64_t{0b1111});
    expect_eq(it.next(), uint8_t{0}, "bit 0");
    expect_eq(it.next(), uint8_t{1}, "bit 1");
    expect_eq(it.next(), uint8_t{2}, "bit 2");
    expect_eq(it.next(), uint8_t{3}, "bit 3");
    expect_eq(it.next(), it.npos, "exhausted -> npos");

    std::cout << "  PASS: test_bit_iterator_consecutive_bits" << std::endl;
}

void test_bit_iterator_sparse_bits() {
    // Bits 0, 4, 7 set: 0b10010001 = 0x91
    bit_iterator<uint32_t, uint8_t> it(uint32_t{0b10010001});
    expect_eq(it.next(), uint8_t{0}, "bit 0");
    expect_eq(it.next(), uint8_t{4}, "bit 4");
    expect_eq(it.next(), uint8_t{7}, "bit 7");
    expect_eq(it.next(), it.npos, "exhausted -> npos");

    std::cout << "  PASS: test_bit_iterator_sparse_bits" << std::endl;
}

void test_bit_iterator_reset() {
    bit_iterator<uint64_t, uint8_t> it(uint64_t{0b1010});  // bits 1, 3
    expect_eq(it.next(), uint8_t{1}, "first pass: bit 1");
    expect_eq(it.next(), uint8_t{3}, "first pass: bit 3");
    expect_eq(it.next(), it.npos,  "first pass exhausted");

    it.reset(uint64_t{0b100});   // bit 2 only
    expect_eq(it.next(), uint8_t{2}, "after reset: bit 2");
    expect_eq(it.next(), it.npos,   "after reset exhausted");

    std::cout << "  PASS: test_bit_iterator_reset" << std::endl;
}

void test_bit_iterator_all_64_bits() {
    uint64_t all = std::numeric_limits<uint64_t>::max();
    bit_iterator<uint64_t, uint8_t> it(all);
    for (uint8_t i = 0; i < 64; ++i) {
        auto idx = it.next();
        // Allow comparing uint8_t as int to avoid printing char values
        if (idx != i) {
            std::string msg = "all bits: expected " + std::to_string(i)
                            + " but got " + std::to_string(idx);
            throw test_error(msg);
        }
        ++tests_passed;
    }
    expect_eq(it.next(), it.npos, "all bits exhausted after 64 iterates");

    std::cout << "  PASS: test_bit_iterator_all_64_bits" << std::endl;
}

void test_bit_iterator_alternating_bits() {
    uint64_t pattern = 0xAAAAAAAAAAAAAAAAULL;  // 1010...1010 — odd bits set
    bit_iterator<uint64_t, uint8_t> it(pattern);
    for (uint8_t i = 1; i < 64; i += 2) {
        auto idx = it.next();
        if (idx != i) {
            std::string msg = "alternating: expected " + std::to_string(i)
                            + " but got " + std::to_string(idx);
            throw test_error(msg);
        }
        ++tests_passed;
    }
    expect_eq(it.next(), it.npos, "alternating exhausted after 32 iterates");

    std::cout << "  PASS: test_bit_iterator_alternating_bits" << std::endl;
}

void test_bit_iterator_uint8_storage() {
    // Template parameter T can be uint8_t
    bit_iterator<uint8_t, uint8_t> it(uint8_t{0b00100100});  // bits 2, 5
    expect_eq(it.next(), uint8_t{2}, "uint8: bit 2");
    expect_eq(it.next(), uint8_t{5}, "uint8: bit 5");
    expect_eq(it.next(), it.npos,   "uint8 exhausted");

    std::cout << "  PASS: test_bit_iterator_uint8_storage" << std::endl;
}

void test_bit_iterator_uint16_storage() {
    bit_iterator<uint16_t, uint8_t> it(uint16_t{0x8100});  // bits 8, 15
    expect_eq(it.next(), uint8_t{8},  "uint16: bit 8");
    expect_eq(it.next(), uint8_t{15}, "uint16: bit 15");
    expect_eq(it.next(), it.npos,     "uint16 exhausted");

    std::cout << "  PASS: test_bit_iterator_uint16_storage" << std::endl;
}

void test_bit_iterator_default_construct() {
    bit_iterator<uint64_t> it;  // default: starts at 0 with remaining_=0
    expect_eq(it.next(), it.npos, "default-constructed iterator returns npos");

    std::cout << "  PASS: test_bit_iterator_default_construct" << std::endl;
}

// ===========================================================================
// sbit_iterator — iterator-style iteration (operator*, ++, bool)
// ===========================================================================

void test_sbit_iterator_empty() {
    sbit_iterator<uint64_t, uint8_t> it(uint64_t{0});
    expect(!static_cast<bool>(it), "empty sbit_iterator is falsy");
    expect_eq(it.get(), it.npos, "empty sbit_iterator::get() == npos");

    std::cout << "  PASS: test_sbit_iterator_empty" << std::endl;
}

void test_sbit_iterator_single_bit() {
    sbit_iterator<uint64_t, uint8_t> it(uint64_t{1} << 5);  // bit 5
    expect(static_cast<bool>(it), "sbit_iterator with bit 5 is truthy");
    expect_eq(*it, uint8_t{5}, "operator* returns current index (5)");
    expect_eq(it.get(), uint8_t{5}, "get() returns current index (5)");

    ++it;  // advance past bit 5
    expect(!static_cast<bool>(it), "exhausted sbit_iterator is falsy");
    expect_eq(it.get(), it.npos, "exhausted get() == npos");

    std::cout << "  PASS: test_sbit_iterator_single_bit" << std::endl;
}

void test_sbit_iterator_multiple_bits() {
    // Bits 2, 5, 7: 0b10100100
    sbit_iterator<uint64_t, uint8_t> it(uint64_t{0b10100100});
    expect(static_cast<bool>(it), "truthy before first deref");

    expect_eq(*it, uint8_t{2}, "first: bit 2"); ++it;
    expect(static_cast<bool>(it), "still truthy after first advance");
    expect_eq(*it, uint8_t{5}, "second: bit 5"); ++it;
    expect_eq(*it, uint8_t{7}, "third: bit 7"); ++it;

    expect(!static_cast<bool>(it), "falsy after consuming all bits");
    expect_eq(it.get(), it.npos, "exhausted get() == npos");

    std::cout << "  PASS: test_sbit_iterator_multiple_bits" << std::endl;
}

void test_sbit_iterator_post_increment() {
    sbit_iterator<uint64_t, uint8_t> it(uint64_t{0b001100});  // bits 2, 3
    uint8_t val = it++;  // post-increment returns previous value
    expect_eq(val, uint8_t{2}, "post-increment returns bit 2");
    expect_eq(*it, uint8_t{3}, "after post-increment, iterator at bit 3");

    std::cout << "  PASS: test_sbit_iterator_post_increment" << std::endl;
}

void test_sbit_iterator_reset() {
    sbit_iterator<uint64_t, uint8_t> it(uint64_t{0b1010});  // bits 1, 3
    expect_eq(*it, uint8_t{1}, "first pass: bit 1"); ++it;
    expect_eq(*it, uint8_t{3}, "first pass: bit 3"); ++it;
    expect(!static_cast<bool>(it), "first pass exhausted");

    uint8_t first = it.reset(uint64_t{0b10001});  // bits 0, 4
    expect_eq(first, uint8_t{0}, "reset() returns first bit 0");
    expect_eq(*it, uint8_t{0},  "after reset: bit 0"); ++it;
    expect_eq(*it, uint8_t{4},  "after reset: bit 4"); ++it;
    expect(!static_cast<bool>(it), "second pass exhausted");

    std::cout << "  PASS: test_sbit_iterator_reset" << std::endl;
}

void test_sbit_iterator_default_construct() {
    sbit_iterator<uint64_t, uint8_t> it;
    expect(!static_cast<bool>(it), "default-constructed sbit_iterator is falsy");
    expect_eq(it.get(), it.npos, "default-constructed get() == npos");

    std::cout << "  PASS: test_sbit_iterator_default_construct" << std::endl;
}

void test_sbit_iterator_for_loop_pattern() {
    // Emulate the common usage pattern: for (sbit_iterator it(mask); it; ++it)
    uint64_t mask = 0b100010001;  // bits 0, 4, 8
    uint8_t expected[] = {0, 4, 8};
    size_t count = 0;
    for (sbit_iterator<uint64_t, uint8_t> it(mask); it; ++it) {
        if (count >= 3) {
            throw test_error("for-loop: more iterations than expected");
        }
        if (*it != expected[count]) {
            std::string msg = "for-loop: expected index "
                            + std::to_string(expected[count])
                            + " but got " + std::to_string(*it);
            throw test_error(msg);
        }
        ++tests_passed;
        ++count;
    }
    expect_eq(count, size_t{3}, "for-loop visited exactly 3 bits");

    std::cout << "  PASS: test_sbit_iterator_for_loop_pattern" << std::endl;
}

void test_sbit_iterator_for_range() {
    // Range-for loop using Sentinels (begin()/end())
    uint64_t mask = 0b10000100001;  // bits 0, 5, 10
    uint8_t expected[] = {0, 5, 10};
    size_t count = 0;
    for (auto idx : sbit_iterator<uint64_t, uint8_t>(mask)) {
        if (count >= 3) {
            throw test_error("for-range: more iterations than expected");
        }
        if (idx != expected[count]) {
            std::string msg = "for-range: expected "
                            + std::to_string(expected[count])
                            + " but got " + std::to_string(idx);
            throw test_error(msg);
        }
        ++tests_passed;
        ++count;
    }
    expect_eq(count, size_t{3}, "for-range visited exactly 3 bits");

    std::cout << "  PASS: test_sbit_iterator_for_range" << std::endl;
}

void test_sbit_iterator_all_64_bits() {
    sbit_iterator<uint64_t, uint8_t> it(std::numeric_limits<uint64_t>::max());
    for (uint8_t i = 0; i < 64; ++i, ++it) {
        if (*it != i) {
            std::string msg = "all bits: expected " + std::to_string(i)
                            + " but got " + std::to_string(*it);
            throw test_error(msg);
        }
        ++tests_passed;
    }
    expect(!static_cast<bool>(it), "all bits exhausted after ++it x64");

    std::cout << "  PASS: test_sbit_iterator_all_64_bits" << std::endl;
}

void test_sbit_iterator_npos_constexpr() {
    // Verify npos is the expected sentinel value
    constexpr auto npos = sbit_iterator<uint64_t, uint8_t>::npos;
    expect_eq(npos, uint8_t{255}, "sbit_iterator::npos == 255 (max of uint8_t)");

    std::cout << "  PASS: test_sbit_iterator_npos_constexpr" << std::endl;
}

// ===========================================================================
// Edge cases and stress
// ===========================================================================

void test_sbit_iterator_single_bit_63_then_reset() {
    // Bit 63 then reset to bit 0
    sbit_iterator<uint64_t, uint8_t> it(uint64_t{1} << 63);
    expect_eq(*it, uint8_t{63}, "bit 63");

    it.reset(uint64_t{1});  // bit 0
    expect_eq(*it, uint8_t{0}, "reset to bit 0");

    ++it;
    expect(!static_cast<bool>(it), "exhausted after single-bit reset");

    std::cout << "  PASS: test_sbit_iterator_single_bit_63_then_reset" << std::endl;
}

void test_bit_iterator_default_construct_npos() {
    // Verify bit_iterator::npos is consistent with sbit_iterator::npos
    static_assert(bit_iterator<uint64_t>::npos ==
                  sbit_iterator<uint64_t>::npos,
                  "bit_iterator::npos and sbit_iterator::npos must match");
    std::cout << "  PASS: test_bit_iterator_default_construct_npos" << std::endl;
}

} // anonymous namespace

int main() {
    std::cout << "\n=== bit_iterator tests ===\n" << std::endl;

    // ── bit_iterator (next-based) ──
    test_bit_iterator_empty();
    test_bit_iterator_single_bit_low();
    test_bit_iterator_single_bit_high();
    test_bit_iterator_single_bit_mid();
    test_bit_iterator_consecutive_bits();
    test_bit_iterator_sparse_bits();
    test_bit_iterator_reset();
    test_bit_iterator_all_64_bits();
    test_bit_iterator_alternating_bits();
    test_bit_iterator_uint8_storage();
    test_bit_iterator_uint16_storage();
    test_bit_iterator_default_construct();

    std::cout << "\n=== sbit_iterator tests ===\n" << std::endl;

    test_sbit_iterator_empty();
    test_sbit_iterator_single_bit();
    test_sbit_iterator_multiple_bits();
    test_sbit_iterator_post_increment();
    test_sbit_iterator_reset();
    test_sbit_iterator_default_construct();
    test_sbit_iterator_for_loop_pattern();
    test_sbit_iterator_for_range();
    test_sbit_iterator_all_64_bits();
    test_sbit_iterator_npos_constexpr();

    std::cout << "\n=== Edge cases ===\n" << std::endl;

    test_sbit_iterator_single_bit_63_then_reset();
    test_bit_iterator_default_construct_npos();

    return print_summary();
}
