#pragma once
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "domain/algorithm/registries/EnchReg.h"
#include <chrono>
#include <cstdint>
#include <vector>

/// Multi-tier merge group-by-multiplier strategy.
///
/// Phase 1 (dedup): Pairwise merge of same-enchant + same-level books
///   to reduce the number of books before structured merging.
/// Phase 2 (grouping): Partition books by effective multiplier (low ≤1,
///   mid ≤2, high >2) and merge within each group.
/// Phase 3 (groovy merge): Merge group-results together, then apply to
///   equipment.
///
/// The multiplier-grouping heuristic reduces PPN growth for expensive
/// books by keeping them separate from cheap ones until late in the
/// merge process.  For ≥8 enchantments this often beats Hamming but is
/// still a construction heuristic — no optimality guarantees.
namespace algorithm {

class HierarchicalMergeAlgorithm : public IAlgorithm {
public:
    /// When book count exceeds this threshold, enable pairwise dedup pass.
    /// Tuned empirically — too low triggers overhead for small inputs.
    static constexpr size_t kDedupThreshold = 7;

    explicit HierarchicalMergeAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "hierarchical"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;
    AlgorithmMode supported_mode() const noexcept override {
        return AlgorithmMode::direct;
    }

private:
    Item merge_group(
        std::vector<Item>& group,
        std::vector<EnchStep>& steps,
        const EnchReg& reg,
        ExecutionContext& ctx,
        const std::chrono::steady_clock::time_point& start,
        const SearchConfig& search);

    int32_t effective_multiplier(const Item& item, const EnchReg& reg) const;

    ForgeEngine _forge_engine;
    AlgorithmDiagnostics _diag;
};

// ── Compile-time checks ─────────────────────────────────────────────────
static_assert(std::is_nothrow_destructible_v<HierarchicalMergeAlgorithm>,
    "HierarchicalMergeAlgorithm: destructor must not throw");

} // namespace algorithm
