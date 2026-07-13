#pragma once
#include "../IAlgorithm.h"
#include "../forge/ForgeEngine.h"
#include "algorithm/components/AlgorithmDiagnostics.h"
#include "registries/CompactedRegistries.h"
#include <cstdint>
#include <vector>

class GreedyAlgorithm : public IAlgorithm {
public:
    explicit GreedyAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "greedy"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;
    bool simulate(const AlgorithmInput& input) const noexcept override;

private:
    struct BookCost {
        size_t index;
        int32_t est_cost;
    };

    ForgeEngine _forge_engine;
    std::vector<compact::Ench> _target;
    AlgorithmDiagnostics _diag;
};
