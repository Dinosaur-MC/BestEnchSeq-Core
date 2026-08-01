#pragma once
#include "Item.h"
#include "common/serialization/IBinarySerializable.h"

namespace algorithm {
struct EnchStep : IBinarySerializable {
    Item base;          // 锻造前的目标物品
    Item sacrifice;     // 锻造前的祭品
    Item result;        // 锻造后的结果物品（解的最后一步 result 即 final item）
    int32_t cost = 0;   // 经验等级消耗

    EnchStep() = default;
    EnchStep(Item base_, Item sacrifice_, Item result_, int32_t cost_) noexcept
        : base(std::move(base_)), sacrifice(std::move(sacrifice_)),
          result(std::move(result_)), cost(cost_) {}

    void serialize(ByteStreamWriter &w) const noexcept override {
        w << base << sacrifice << result << cost;
    }
    void deserialize(ByteStreamReader &r) noexcept override {
        r >> base >> sacrifice >> result >> cost;
    }
};

/// A complete solution: ordered forge steps + total cost.
struct EnchSolution : IBinarySerializable {
    std::vector<EnchStep> steps;
    int32_t total_cost{0};

    EnchSolution() = default;
    EnchSolution(std::vector<EnchStep> steps_, int32_t total_cost_) noexcept
        : steps(std::move(steps_)), total_cost(total_cost_) {}

    void serialize(ByteStreamWriter &w) const noexcept override {
        w << steps << total_cost;
    }
    void deserialize(ByteStreamReader &r) noexcept override {
        r >> steps >> total_cost;
    }
};
} // namespace algorithm
