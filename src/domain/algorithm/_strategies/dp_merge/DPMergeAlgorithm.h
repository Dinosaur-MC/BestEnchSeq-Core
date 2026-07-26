#pragma once
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "domain/algorithm/registries/EnchReg.h"
#include <cstdint>
#include <vector>

/// Divide-and-conquer DP merge-order optimizer (port of enchant-order).
///
/// Algorithm:
///   1. Enumerate all 2-partitions of the current item set (Catalan split).
///   2. Recursively solve each partition (memoized).
///   3. Combine sub-results via forge_into, try both base/sacrifice directions.
///   4. Bucket by (EnchSet, PPN): within each equivalence class, keep only
///      the cheapest entry.  This is optimality-safe because items with the
///      same EnchSet at the same PPN are strictly interchangeable:
///      future forge cost depends only on EnchSet (via mul_b) and PPN
///      (via 2^ppn - 1), not on the merge history.
///
/// The DP explores all O(Catalan(N)) merge topologies, but memoization
/// collapses identical subproblems and bucketing prunes dominated states.
/// For N ≤ 10 this completes in milliseconds; for N ≤ 12 it is competitive
/// with A* while often producing tighter solutions faster.
namespace algorithm {

class DPMergeAlgorithm : public IAlgorithm {
public:
    explicit DPMergeAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "dp_merge"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    void execute(const AlgorithmInput& input, ExecutionContext& ctx) override;
    AlgorithmMode supported_mode() const noexcept override {
        return AlgorithmMode::direct;
    }

private:
    // ── Pareto entry: a merge result at a specific (EnchSet, PPN) ──────
    struct ParetoEntry {
        int32_t cost{0};           // total cumulative cost (levels)
        uint8_t ppn{0};            // prior work penalty of the result item
        Item item;                 // the resulting item
        std::vector<EnchStep> steps; // merge steps to produce this item

        ParetoEntry() = default;
        ParetoEntry(int32_t c, uint8_t p, Item i, std::vector<EnchStep> s)
            : cost(c), ppn(p), item(std::move(i)), steps(std::move(s)) {}
    };

    // ── Frontier: compressed set of Pareto-non-dominated entries ───────
    //
    // Keeps, for each (EnchSet, PPN) pair, only the entry with the lowest
    // cumulative cost.  This is safe because items with identical EnchSets
    // at the same PPN are functionally interchangeable for any future forge.
    struct Frontier {
        std::vector<ParetoEntry> entries;

        /// Insert or merge \p entry at its (EnchSet, PPN) equivalence class.
        /// If a cheaper entry exists for the same class, the new one is
        /// discarded.  If the new one is cheaper, it replaces the old one.
        void insert(ParetoEntry entry);

        bool empty() const { return entries.empty(); }
    };

    // Top-level cache: item-set → Pareto frontier.
    // std::hash<ItemCollection> is provided by Item.h.
    // std::unordered_map handles collisions via element-wise Item equality.
    std::unordered_map<ItemCollection, Frontier> _cache;

    ForgeEngine _forge_engine;
    const EnchReg* _ench_reg{nullptr};
    std::vector<Ench> _target;

    AlgorithmDiagnostics _diag;

    // ── DP core ───────────────────────────────────────────────────────
    Frontier solve(std::vector<Item> items);

    /// Sort items into deterministic order: Equip first, then by (PPN, ench).
    static void canonicalize(std::vector<Item>& items) noexcept;
};

} // namespace algorithm
