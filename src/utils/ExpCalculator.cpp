#include "ExpCalculator.hpp"

int32_t ExpCalculator::level_to_exp(int32_t level) noexcept {
    // Same formula as old Utils::calc_exp
    if (level <= 16)
        return level * level + 6 * level;
    else if (level <= 31)
        return (5 * level * level - 81 * level + 720) / 2;
    else
        return (9 * level * level - 325 * level + 4440) / 2;
}

int32_t ExpCalculator::peak_level_cost(const EnchStepList& steps) noexcept {
    if (steps.empty()) return 0;
    int32_t peak = 0;
    for (auto& s : steps)
        if (s.exp_level_cost > peak) peak = s.exp_level_cost;
    return peak;
}

int32_t ExpCalculator::peak_exp_cost(const EnchStepList& steps) noexcept {
    if (steps.empty()) return 0;
    int32_t peak = 0;
    for (auto& s : steps) {
        if (s.exp_cost > peak) peak = s.exp_cost;
    }
    return peak;
}
