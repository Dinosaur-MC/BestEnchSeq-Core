#pragma once
#include "Item.h"

namespace algorithm {
struct EnchStep {
    Item base;      // 锻造前的目标物品
    Item sacrifice; // 锻造前的祭品
    int32_t cost;   // 经验等级消耗
};

/// A complete solution: ordered forge steps + total cost.
struct EnchSolution {
    std::vector<EnchStep> steps;
    int32_t total_cost{0};
};
} // namespace algorithm
