#pragma once
#include "../IAlgorithm.h"
#include "../DefaultForgeEngine.h"
#include "../CompactForgeEngine.h"
#include "registries/CompactedRegistries.h"
#include <cstdint>
#include <vector>

class CompactHierarchicalMergeStrategy : public IAlgorithm {
public:
    explicit CompactHierarchicalMergeStrategy(ForgeConfig forge_cfg = {})
        : _forge_engine(forge_cfg.ignore_penalty_cost, forge_cfg.ignore_cost_cap) {}

    std::string_view name() const noexcept override { return "compact_hierarchical"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    const IForgeEngine& forge_engine() const noexcept override {
        static DefaultForgeEngine fallback;
        return fallback;
    }

    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;

private:
    // Merge a group of compact items into a single stack (penalty-closest).
    compact::Item merge_group(
        std::vector<compact::Item>& group,
        std::vector<compact::EnchStep>& steps,
        const compact::EnchReg& reg,
        ExecutionContext& ctx);

    static int32_t effective_multiplier(const compact::Item& item, const compact::EnchReg& reg);

    compact::CompactForgeEngine _forge_engine;
};
