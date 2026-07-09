#pragma once
#include "../IAlgorithm.h"
#include "../DefaultForgeEngine.h"
#include "../CompactForgeEngine.h"
#include "registries/CompactedRegistries.h"
#include "utils/CompactAdapter.hpp"
#include <cstdint>
#include <vector>

/// Greedy algorithm using compact internal representation.
///
/// Proof-of-concept: same strategy as GreedyAlgorithm but operates on
/// compact::Item internally for faster cost estimation and forge operations.
/// Converts to domain types only for step recording.
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
        size_t index;      // index in the compact items vector (offset by 1 from equipment)
        int32_t est_cost;
    };

    compact::CompactForgeEngine _forge_engine;
};
