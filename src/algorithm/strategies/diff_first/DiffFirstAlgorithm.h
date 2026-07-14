#pragma once
#include "algorithm/IAlgorithm.h"
#include "algorithm/forge/ForgeEngine.h"
#include "algorithm/diagnostics/AlgorithmDiagnostics.h"

/// Penalty-tiered merge strategy (port of Alg_DifficultyFirst from v2.x).
///
/// Processes items bottom-up by PPN tier.  Within each tier, forges the
/// cheapest books together first, or merges the cheapest book into the
/// equipment when the equipment is at that tier.  Switches to a linear
/// merge pass once all tiers have been visited.
///
/// Unlike Hamming's expensive-first global balanced tree, DiffFirst uses
/// a cheapest-first local heuristic that can produce different — sometimes
/// complementary — merge topologies.
///
/// O(n²) worst-case due to the scan inside the main loop.
class DiffFirstAlgorithm : public IAlgorithm {
public:
    explicit DiffFirstAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "difficulty_first"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;
    bool simulate(const AlgorithmInput& input) const noexcept override;

private:
    ForgeEngine _forge_engine;
    AlgorithmDiagnostics _diag;
};
