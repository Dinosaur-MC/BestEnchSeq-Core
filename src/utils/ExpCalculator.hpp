#pragma once
#include "../types/EnchSolution.h"
#include <cstdint>

class ExpCalculator {
public:
    // 经验等级 → 经验值（原 Utils::calc_exp）
    static int32_t level_to_exp(int32_t level) noexcept {
        if (level <= 16)
            return level * level + 6 * level;
        else if (level <= 31)
            return (5 * level * level - 81 * level + 720) / 2;
        else
            return (9 * level * level - 325 * level + 4440) / 2;
    }

    // 峰值成本分析
    static int32_t peak_level_cost(const EnchStepList& steps) noexcept {
        if (steps.empty()) return 0;
        int32_t peak = 0;
        for (auto& s : steps)
            if (s.exp_level_cost > peak) peak = s.exp_level_cost;
        return peak;
    }

    static int32_t peak_exp_cost(const EnchStepList& steps) noexcept {
        if (steps.empty()) return 0;
        int32_t peak = 0;
        for (auto& s : steps) {
            int32_t exp = level_to_exp(s.exp_level_cost);
            if (exp > peak) peak = exp;
        }
        return peak;
    }
};
