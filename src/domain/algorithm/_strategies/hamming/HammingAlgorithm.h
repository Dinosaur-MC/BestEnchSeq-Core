#pragma once
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "domain/algorithm/registries/EnchReg.h"
#include <vector>

/// Popcount-based balanced merge tree algorithm.
///
/// Arranges items using Hamming-weight (popcount) ordering to form a
/// balanced binary merge tree.  This minimises penalty growth (2^ppn - 1)
/// — the dominant cost factor — by keeping each item's merge depth at
/// ceil(log2(n)).
///
/// Algorithm (port of classic Hamming merge from BestEnchSeq v2.x):
///   1. Place all items at tier 0 (their starting PPN tier).
///   2. For the current tier: sort by forge cost descending (expensive
///      books merge earliest into the equipment, avoiding extra penalty).
///   3. Within the tier, arrange items by popcount index so that position
///      k goes through roughly popcount(k) merges — a balanced tournament
///      bracket.
///   4. Pairwise-forge items at the current tier; send all results
///      (including any odd left-over) to the *next* sequential tier
///      (tier+1), guaranteeing everything converges.
///   5. Repeat from step 2 until a single tier remains with one item.
///
/// O(n log n) deterministic construction, no backtracking.  Produces an
/// upper bound that often matches or beats penalty_balance, especially
/// for ≥8 enchantments where search-based algorithms slow significantly.
namespace algorithm {
class HammingAlgorithm : public IAlgorithm {
public:
    explicit HammingAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "hamming"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    double evaluate(int16_t ench_count) const noexcept override;
    void execute(const AlgorithmInput &input, ExecutionContext& ctx) override;
    std::unique_ptr<IForgeEngine> get_forge_engine() const noexcept override {
        return std::make_unique<ForgeEngine>(_forge_engine);
    }
    AlgorithmMode supported_mode() const noexcept override {
        return AlgorithmMode::direct | AlgorithmMode::inventory;
    }

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
    void arrange_by_popcount(std::vector<Item>& items,
                              const EnchReg& reg) const;

    // ── Members ─────────────────────────────────────────────────────────

    ForgeEngine _forge_engine;
    const EnchReg *_ench_reg{nullptr};
    AlgorithmDiagnostics _diag;
};

// ── Compile-time checks ─────────────────────────────────────────────────
static_assert(std::is_nothrow_destructible_v<HammingAlgorithm>,
    "HammingAlgorithm: destructor must not throw");
static_assert(sizeof(HammingAlgorithm) < 2048,
    "HammingAlgorithm: size exceeds expected range — check for member bloat");

} // namespace algorithm
