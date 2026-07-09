#pragma once
#include "../IAlgorithm.h"
#include "../forge/ForgeEngine.h"
#include "registries/CompactedRegistries.h"
#include <cstdint>
#include <vector>

class HierarchicalMergeStrategy : public IAlgorithm {
public:
    explicit HierarchicalMergeStrategy(bool ignore_penalty_cost = false,
                                               bool ignore_cost_cap = false) noexcept
        : _forge_engine(ignore_penalty_cost, ignore_cost_cap) {}

    std::string_view name() const noexcept override { return "compact_hierarchical"; }
    std::string_view version() const noexcept override { return "1.0.0"; }

    void execute(
        const std::vector<compact::Item>& items,
        const compact::EnchReg& reg,
        const std::vector<compact::Ench>& target,
        ExecutionContext& ctx
    ) override;

private:
    compact::Item merge_group(
        std::vector<compact::Item>& group,
        std::vector<compact::EnchStep>& steps,
        const compact::EnchReg& reg,
        ExecutionContext& ctx);

    static int32_t effective_multiplier(const compact::Item& item, const compact::EnchReg& reg);

    compact::ForgeEngine _forge_engine;
};
