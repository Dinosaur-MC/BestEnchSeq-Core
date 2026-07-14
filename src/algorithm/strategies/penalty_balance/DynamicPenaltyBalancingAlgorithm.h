#pragma once
#include "algorithm/IAlgorithm.h"
#include "algorithm/forge/ForgeEngine.h"
#include "algorithm/diagnostics/AlgorithmDiagnostics.h"

class DynamicPenaltyBalancingAlgorithm : public IAlgorithm {
public:
    explicit DynamicPenaltyBalancingAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "penalty_balance"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;

private:
    ForgeEngine _forge_engine;
    AlgorithmDiagnostics _diag;
};
