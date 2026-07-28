#pragma once
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"

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
namespace algorithm {

class DiffFirstAlgorithm : public IAlgorithm {
public:
    explicit DiffFirstAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "difficulty_first"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    int64_t evaluate(int16_t ench_count) const noexcept override;
    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;
    std::optional<Item> process(const EnchSolution &solution) const override;
    std::unique_ptr<IForgeEngine> get_forge_engine() const noexcept override {
        return std::make_unique<ForgeEngine>(_forge_engine);
    }
    bool simulate(const AlgorithmInput& input) const noexcept override;
    AlgorithmMode supported_mode() const noexcept override {
        return AlgorithmMode::direct;
    }

private:
    ForgeEngine _forge_engine;
    const EnchReg *_ench_reg{nullptr};
    AlgorithmDiagnostics _diag;
};

// ── Compile-time checks ─────────────────────────────────────────────────
static_assert(std::is_nothrow_destructible_v<DiffFirstAlgorithm>,
    "DiffFirstAlgorithm: destructor must not throw");

} // namespace algorithm
