#include "test_utils.h"
#include "utils/AlgorithmUtils.hpp"

void test_hash_combine() {
    size_t h = 0;
    AlgorithmUtils::hash_combine(h, 42);
    expect(h != 0, "hash combine should produce non-zero result");

    size_t h2 = 0;
    AlgorithmUtils::hash_combine(h2, 42);
    expect(h == h2, "hash combine should be deterministic");

    std::cout << "PASS: test_hash_combine" << std::endl;
}

int main() {
    std::cout << "=== AlgorithmUtils Tests ===" << std::endl;
    test_hash_combine();
    std::cout << "All AlgorithmUtils tests passed!" << std::endl;
    return 0;
}
