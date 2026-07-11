#pragma once
#include "../IAlgorithm.h"
#include "../forge/ForgeEngine.h"
#include "registries/CompactedRegistries.h"
#include <vector>

class DynamicPenaltyBalancingAlgorithm : public IAlgorithm {
public:
    explicit DynamicPenaltyBalancingAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "penalty_balance"; }
    std::string_view version() const noexcept override { return "1.0.0"; }

    void execute(
        const std::vector<compact::Item>& items,
        const compact::EnchReg& reg,
        const std::vector<compact::Ench>& target,
        ExecutionContext& ctx
    ) override;

private:
    ForgeEngine _forge_engine;
};
