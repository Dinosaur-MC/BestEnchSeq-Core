#pragma once
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

    // 峰值成本分析（通用迭代器接口，零域类型依赖）
    template <typename StepIter>
    static int32_t peak_level_cost(StepIter begin, StepIter end) noexcept {
        int32_t peak = 0;
        for (; begin != end; ++begin)
            if (begin->exp_level_cost > peak) peak = begin->exp_level_cost;
        return peak;
    }

    template <typename StepIter>
    static int32_t peak_exp_cost(StepIter begin, StepIter end) noexcept {
        int32_t peak = 0;
        for (; begin != end; ++begin) {
            int32_t exp = begin->exp_cost;
            if (exp > peak) peak = exp;
        }
        return peak;
    }
};
