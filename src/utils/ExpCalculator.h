#pragma once
#include "../types/EnchSolution.h"
#include <cstdint>

class ExpCalculator {
public:
    static int32_t level_to_exp(int32_t level) noexcept;
    static int32_t peak_level_cost(const EnchStepList& steps) noexcept;
    static int32_t peak_exp_cost(const EnchStepList& steps) noexcept;
};
