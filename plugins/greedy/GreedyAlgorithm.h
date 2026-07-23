#pragma once
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"
#include <cstdint>
#include <vector>

namespace algorithm {

class GreedyAlgorithm : public IAlgorithm {
public:
    explicit GreedyAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "greedy"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;
    bool simulate(const AlgorithmInput& input) const noexcept override;
    AlgorithmMode supported_mode() const noexcept override {
        return AlgorithmMode::direct | AlgorithmMode::inventory;
    }

private:
    struct BookCost {
        size_t index;
        int32_t est_cost;
    };

    ForgeEngine _forge_engine;
    std::vector<Ench> _target;
    AlgorithmDiagnostics _diag;
};

} // namespace algorithm
