#pragma once
#include "../IAlgorithm.h"
#include "../forge/ForgeEngine.h"
#include "registries/CompactedRegistries.h"
#include <cstdint>
#include <vector>

class GreedyAlgorithm : public IAlgorithm {
public:
    explicit GreedyAlgorithm(bool ignore_penalty_cost = false,
                                     bool ignore_cost_cap = false) noexcept
        : _forge_engine(ignore_penalty_cost, ignore_cost_cap) {}

    std::string_view name() const noexcept override { return "compact_greedy"; }
    std::string_view version() const noexcept override { return "1.0.0"; }

    void execute(
        const std::vector<compact::Item>& items,
        const compact::EnchReg& reg,
        const std::vector<compact::Ench>& target,
        ExecutionContext& ctx
    ) override;

private:
    struct BookCost {
        size_t index;
        int32_t est_cost;
    };

    ForgeEngine _forge_engine;
};
