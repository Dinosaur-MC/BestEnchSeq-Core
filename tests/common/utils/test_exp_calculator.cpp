#define BESQ_TEST_MAIN
#include "framework/test_framework.h"
#include "domain/business/types/Solution.h"
#include "utils/ExpCalculator.hpp"
#include <vector>

TEST_CASE("test_level_to_exp") {
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

TEST_CASE("test_peak_analysis") {
    std::vector<Solution::EnchStep> steps;
    // Empty
    expect(ExpCalculator::peak_level_cost(steps.begin(), steps.end()) == 0, "empty → peak 0");

    // Add some steps
    Solution::EnchStep s1{{}, {}, 4, ExpCalculator::level_to_exp(4)};
    Solution::EnchStep s2{{}, {}, 10, ExpCalculator::level_to_exp(10)};
    Solution::EnchStep s3{{}, {}, 7, ExpCalculator::level_to_exp(7)};
    steps.push_back(s1);
    steps.push_back(s2);
    steps.push_back(s3);

    expect(ExpCalculator::peak_level_cost(steps.begin(), steps.end()) == 10, "peak level cost should be 10");
    expect(ExpCalculator::peak_exp_cost(steps.begin(), steps.end()) == ExpCalculator::level_to_exp(10),
           "peak exp cost should match level_to_exp(10)");
    std::cout << "PASS: test_peak_analysis" << std::endl;
}

