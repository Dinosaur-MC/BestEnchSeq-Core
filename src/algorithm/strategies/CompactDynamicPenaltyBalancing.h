#pragma once
#include "../IAlgorithm.h"
#include "../forge/DefaultForgeEngine.h"
#include "../forge/CompactForgeEngine.h"
#include "registries/CompactedRegistries.h"
#include <cstdint>
#include <vector>

class CompactDynamicPenaltyBalancing : public IAlgorithm {
public:
    explicit CompactDynamicPenaltyBalancing(ForgeConfig forge_cfg = {})
        : _forge_engine(forge_cfg.ignore_penalty_cost, forge_cfg.ignore_cost_cap) {}

    std::string_view name() const noexcept override { return "compact_penalty_balance"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    const IForgeEngine& forge_engine() const noexcept override {
        static DefaultForgeEngine fallback;
        return fallback;
    }

    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;

private:
    compact::CompactForgeEngine _forge_engine;
};
