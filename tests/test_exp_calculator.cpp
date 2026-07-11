#include "test_utils.h"
#include "utils/ExpCalculator.hpp"

void test_level_to_exp() {
    // Level 0 → 0
    expect(ExpCalculator::level_to_exp(0) == 0, "level 0 → 0 exp");
    // Level 16 → 16² + 6×16 = 256 + 96 = 352
    expect(ExpCalculator::level_to_exp(16) == 352, "level 16 → 352 exp");
    // Level 17 → (5×289 − 81×17 + 720)/2 = (1445 − 1377 + 720)/2 = 788/2 = 394
    expect(ExpCalculator::level_to_exp(17) == 394, "level 17 → 394 exp");
    // Level 31 → (5×961 − 81×31 + 720)/2 = (4805 − 2511 + 720)/2 = 3014/2 = 1507
    expect(ExpCalculator::level_to_exp(31) == 1507, "level 31 → 1507 exp");
    // Level 32 → (9×1024 − 325×32 + 4440)/2 = (9216 − 10400 + 4440)/2 = 3256/2 = 1628
    expect(ExpCalculator::level_to_exp(32) == 1628, "level 32 → 1628 exp");
    std::cout << "PASS: test_level_to_exp" << std::endl;
}

void test_peak_analysis() {
    EnchStepList steps;
    // Empty
    expect(ExpCalculator::peak_level_cost(steps) == 0, "empty → peak 0");

    // Add some steps
    // exp_cost should match level_to_exp(exp_level_cost) for consistency
    EnchSolution::EnchStep s1{{}, {}, 4, ExpCalculator::level_to_exp(4)};
    EnchSolution::EnchStep s2{{}, {}, 10, ExpCalculator::level_to_exp(10)};
    EnchSolution::EnchStep s3{{}, {}, 7, ExpCalculator::level_to_exp(7)};
    steps.push_back(s1);
    steps.push_back(s2);
    steps.push_back(s3);

    expect(ExpCalculator::peak_level_cost(steps) == 10, "peak level cost should be 10");
    expect(ExpCalculator::peak_exp_cost(steps) == ExpCalculator::level_to_exp(10),
           "peak exp cost should match level_to_exp(10)");
    std::cout << "PASS: test_peak_analysis" << std::endl;
}

int main() {
    try {
        test_level_to_exp();
        test_peak_analysis();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
