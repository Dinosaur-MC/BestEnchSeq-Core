#pragma once
#include "../IAlgorithm.h"
#include "../forge/ForgeEngine.h"
#include "algorithm/components/AlgorithmDiagnostics.h"
#include "registries/CompactedRegistries.h"
#include <vector>

/// Popcount-based balanced merge tree algorithm.
///
/// Arranges items within each penalty tier using Hamming-weight (popcount)
/// ordering to form a balanced binary merge tree. This minimises penalty
/// growth (2^ppn - 1) — the dominant cost factor — by keeping each item's
/// merge depth at ceil(log2(n)).
///
/// Algorithm (port of classic Hamming merge from BestEnchSeq v2.x):
///   1. Group forgeable items by their prior-penalty number (ppn).
///   2. Within each ppn tier, sort by forge cost descending (expensive
///      books merge earliest into the equipment, avoiding extra penalty).
///   3. Arrange items by popcount index so that position k goes through
///      popcount(k) merges — a balanced tournament bracket.
///   4. Pairwise-forge items at the current tier; results bubble up to
///      the next ppn tier.
///   5. Repeat until a single (ideally fully-enchanted) item remains.
///
/// O(n log n) deterministic, no backtracking.  Produces an upper bound
/// that often matches or beats penalty_balance, especially for ≥8
/// enchantments where search-based algorithms begin to slow significantly.
class HammingAlgorithm : public IAlgorithm {
public:
    explicit HammingAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "hamming"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;

    /// Optimistic feasibility — the balanced merge tree is robust and the
    /// execute() path validates the result correctly on all inputs.
    bool simulate(const AlgorithmInput& input) const noexcept override;

private:
    // ── Popcount helpers ────────────────────────────────────────────────

    /// Count set bits (Hamming weight / popcount).
    static int popcount(int x) noexcept {
#if defined(__GNUC__) || defined(__clang__)
        return __builtin_popcount(static_cast<unsigned>(x));
#else
        int c = 0;
        while (x) { c += x & 1; x >>= 1; }
        return c;
#endif
    }

    /// Return all indices k in [0, n-1] where popcount(k) == j.
    static std::vector<int> dup_floor_members(int j, int n) noexcept;

    /// Arrange \p items at one ppn tier into popcount-balanced order.
    /// \p items  in: sorted-by-cost (desc); out: popcount-arranged.
    void arrange_by_popcount(std::vector<compact::Item>& items,
                              const compact::EnchReg& reg) const;

    // ── Members ─────────────────────────────────────────────────────────

    ForgeEngine _forge_engine;
    AlgorithmDiagnostics _diag;
};
