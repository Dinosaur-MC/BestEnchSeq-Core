#pragma once
#include "../IAlgorithm.h"
#include "../DefaultForgeEngine.h"
#include "../CompactForgeEngine.h"
#include "registries/CompactedRegistries.h"
#include <cstdint>
#include <vector>

/// Greedy algorithm using compact internal representation.
///
/// During search, NO domain types are touched. CompactAdapter is used only
/// at the input boundary (prepare) and output boundary (step conversion).
class CompactGreedyAlgorithm : public IAlgorithm {
public:
    explicit CompactGreedyAlgorithm(ForgeConfig forge_cfg = {})
        : _forge_engine(forge_cfg.ignore_penalty_cost, forge_cfg.ignore_cost_cap) {}

    std::string_view name() const noexcept override { return "compact_greedy"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    const IForgeEngine& forge_engine() const noexcept override {
        static DefaultForgeEngine fallback;
        return fallback;
    }

    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;

private:
    struct BookCost {
        size_t index;
        int32_t est_cost;
    };

    struct CompactStep {
        compact::Item base;
        compact::Item sacrifice;
        int32_t cost;
    };

    compact::CompactForgeEngine _forge_engine;
};
