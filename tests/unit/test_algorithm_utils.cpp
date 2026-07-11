#include "framework/test_utils.h"
#include "utils/HashUtils.hpp"

void test_hash_combine() {
    size_t h = 0;
    hash_combine(h, 42);
    expect(h != 0, "hash combine should produce non-zero result");

    size_t h2 = 0;
    hash_combine(h2, 42);
    expect(h == h2, "hash combine should be deterministic");

    // Verify that different values produce different hashes
    size_t h3 = 0;
    hash_combine(h3, 99);
    expect(h != h3, "hash combine should differ for different inputs");

    // Verify accumulative behavior: combining (1,2) != combining (2,1)
    size_t ha = 0;
    hash_combine(ha, 1);
    hash_combine(ha, 2);
    size_t hb = 0;
    hash_combine(hb, 2);
    hash_combine(hb, 1);
    expect(ha != hb, "hash combine should be order-sensitive");

    std::cout << "PASS: test_hash_combine" << std::endl;
}

int main() {
    std::cout << "=== AlgorithmUtils Tests ===" << std::endl;
    try {
        test_hash_combine();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
