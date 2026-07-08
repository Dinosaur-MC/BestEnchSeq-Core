#pragma once
#include "../IAlgorithm.h"
#include "../DefaultForgeEngine.h"

// ─── Dynamic Penalty Balancing ───
//
// Strategy: at each step, select the forgeable pair with the closest
// prior_penalty values. This keeps max(p_i, p_j) growth minimal because
//   P_result = 2 * max(pa, pb) + 1
// grows slowest when pa ≈ pb.
//
// Tie-breaking (3-level sort):
//   1. |penalty_i - penalty_j| ascending (closest penalty first)
//   2. forge_cost_estimate ascending (cheapest among equal-penalty pairs)
//   3. book-book over book-equipment (keep equipment penalty at 0 longer)
//
// Complexity: O(n^2) per step, O(n^3) total. For n ≤ 14, < 0.1ms.
// Expected improvement over naive greedy: 8-12%.

class DynamicPenaltyBalancing : public IAlgorithm {
public:
    explicit DynamicPenaltyBalancing(ForgeConfig forge_cfg = {})
        : _forge_engine(forge_cfg) {}

    std::string_view name() const noexcept override { return "penalty_balance"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    const IForgeEngine& forge_engine() const noexcept override { return _forge_engine; }

    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;

private:
    DefaultForgeEngine _forge_engine;
};
