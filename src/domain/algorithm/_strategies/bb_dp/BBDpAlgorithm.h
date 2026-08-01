#pragma once
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/components/StepTree.h"
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/registries/EnchReg.h"
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace algorithm {

/// Branch-and-bound divide-and-conquer DP merge-order optimizer (bb_dp).
///
/// Improves on DPMergeAlgorithm by combining its exact Catalan-partition DP
/// with the ideas below, targeting "as fast as hamming, as accurate as the
/// exact DP":
///
///   1. Upper-bound (B&B) pruning — a hamming-style deterministic merge
///      (or `SearchConfig.initial_bound` from a warm-up run) provides an
///      achievable total cost; any partial/complete result that cannot beat
///      it is pruned.  Safe: only costs ≥ bound are dropped.
///   2. Optional per-step cap (`SearchConfig.max_step_cost`, vanilla 39) as a
///      SOFT constraint — two-pass search.  The unconstrained optimum runs
///      first (fast path); only if its best solution has a step > cap does a
///      constrained search find the best fully ≤cap solution.  When no ≤cap
///      solution exists it falls back to the unconstrained optimum, so the
///      cap is never a dead end (mods may break the vanilla 39-level limit).
///   3. True Pareto frontiers — an entry is dropped when another entry with
///      the same (type, enchset) has ≤ ppn and ≤ cost, because future merge
///      cost depends only on (type, enchset, ppn).
///   4. Balance-first partition enumeration — partitions with |A| closest to
///      |S|/2 are tried first (balanced trees are strongly preferred by the
///      exponential PPN term), so good solutions appear early and the bound
///      tightens sooner.
///   5. Optional beam width (`SearchConfig.beam_width`) — keeps only the
///      cheapest B frontier entries per subset, trading optimality for speed
///      (B=1 ≈ greedy DP, B=0 = exact).
///
/// Worst-case complexity is exponential (Catalan partitions), but bound +
/// cap + Pareto pruning collapse the space dramatically for N ≤ 24.  Direct
/// mode only (inventory mode is future work).
class BBDpAlgorithm : public IAlgorithm {
public:
    explicit BBDpAlgorithm(ForgeConfig cfg = {}) noexcept
        : _forge_engine(std::move(cfg)) {}

    std::string_view name() const noexcept override { return "bb_dp"; }
    std::string_view version() const noexcept override { return "1.0.0"; }
    double evaluate(int16_t ench_count) const noexcept override;
    void execute(const AlgorithmInput &input, ExecutionContext &ctx) override;
    std::unique_ptr<IForgeEngine> get_forge_engine() const noexcept override {
        return std::make_unique<ForgeEngine>(_forge_engine);
    }
    AlgorithmMode supported_mode() const noexcept override {
        return AlgorithmMode::direct;
    }

private:
    struct ParetoEntry {
        int64_t cost{0};         // total cumulative cost (levels)
        uint8_t ppn{0};          // prior work penalty
        int32_t max_step{0};     // max single-step cost in this history (≤cap check)
        Item item;               // the resulting item
        StepTree step_tree;      // merge history (shared DAG)

        ParetoEntry() = default;
        ParetoEntry(int64_t c, uint8_t p, int32_t ms, Item i, StepTree t)
            : cost(c), ppn(p), max_step(ms), item(std::move(i)), step_tree(std::move(t)) {}
    };

    struct Frontier {
        std::vector<ParetoEntry> entries;

        void insert(ParetoEntry entry, int32_t beam_width);
        bool empty() const { return entries.empty(); }
    };

    ForgeEngine _forge_engine;
    const EnchReg *_ench_reg{nullptr};
    std::vector<Ench> _target;

    // Memoisation cache: item-set → Pareto frontier (pruned at the run's bound).
    std::unordered_map<ItemCollection, Frontier> _cache;
    mutable std::shared_mutex _cache_mutex;

    // Steps of the deterministic hamming-style construction (filled by
    // compute_ub); used as an anytime fallback when the exact search gives up.
    std::vector<EnchStep> _ub_steps;

    AlgorithmDiagnostics _diag;

    Frontier solve(std::vector<Item> items, int32_t max_step_cost,
                   int32_t bound, int32_t beam_width, bool parallelize,
                   bool final_level, ExecutionContext &ctx);
    /// Deterministic balanced-merge upper bound (hamming-style construction).
    /// Records the merge steps into _ub_steps.  Returns INT32_MAX when no
    /// complete solution can be built.
    int32_t compute_ub(std::vector<Item> items);
    static void canonicalize(std::vector<Item> &items) noexcept;

    static constexpr size_t MAX_CACHE_ENTRIES   = 1'000'000;
    static constexpr size_t PARALLEL_THRESHOLD  = 14;
};

static_assert(std::is_nothrow_destructible_v<BBDpAlgorithm>,
              "BBDpAlgorithm: destructor must not throw");
static_assert(!std::is_trivially_copyable_v<BBDpAlgorithm>,
              "BBDpAlgorithm: must not be trivially copyable");
static_assert(sizeof(BBDpAlgorithm) < 8192,
              "BBDpAlgorithm: size exceeds expected range");

} // namespace algorithm
