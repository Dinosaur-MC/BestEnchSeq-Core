#pragma once
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"

/// Dynamic penalty-aware merge pair selection.
///
/// At each step, selects the forgeable pair (i, j) with the smallest PPN
/// difference |ppn_i − ppn_j|, breaking ties by estimated forge cost,
/// then by preferring book-book over equipment-book merges.
///
/// The PPN-difference heuristic keeps the equipment's PPN growth gradual,
/// which often avoids "Too Expensive!" caps for large enchantment sets.
/// O(n²) per merge step — simpler than Hamming's popcount arrangement
/// but can produce competitive results for booksets with mixed multipliers.
namespace algorithm {

class DynamicPenaltyBalancingAlgorithm : public IAlgorithm {
public:
    explicit DynamicPenaltyBalancingAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "penalty_balance"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;
    AlgorithmMode supported_mode() const noexcept override {
        return AlgorithmMode::direct;
    }

private:
    ForgeEngine _forge_engine;
    AlgorithmDiagnostics _diag;
};

// ── Compile-time checks ─────────────────────────────────────────────────
static_assert(std::is_nothrow_destructible_v<DynamicPenaltyBalancingAlgorithm>,
    "DynamicPenaltyBalancingAlgorithm: destructor must not throw");

} // namespace algorithm
