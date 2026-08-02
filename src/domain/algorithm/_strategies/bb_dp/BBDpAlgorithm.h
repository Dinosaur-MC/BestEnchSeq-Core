#pragma once
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/components/StepTree.h"
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/registries/EnchReg.h"
#include <atomic>
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
        /// Pareto-domination drops during this frontier's construction
        /// (single-threaded per frontier; aggregated into the global counter
        /// at cache_put — spec Tier 1).
        uint64_t dropped{0};

        /// Insert under Pareto domination (plus optional beam).  Returns a
        /// pointer to the stored entry when it survived, nullptr when dropped —
        /// the caller uses it to attach the merge-history StepTree lazily,
        /// avoiding a make_shared for the large majority of entries that get
        /// dominated (spec: the tree is only needed for surviving entries).
        ParetoEntry* insert(ParetoEntry entry, int32_t beam_width);
        bool empty() const { return entries.empty(); }
    };

    ForgeEngine _forge_engine;
    const EnchReg *_ench_reg{nullptr};
    Item _target;  // full solve target (type + enchantments)

    // Base items in canonical order — every recursive subproblem is a subset
    // of this array, identified by a bitmask (see `_cache` key).
    std::vector<Item> _base_items;

    // Memoisation cache: subset bitmask → Pareto frontier (pruned at the run's
    // bound).  Direct mode has no duplicate books, so a bitmask over
    // `_base_items` uniquely identifies each subproblem — O(1) hash of a
    // uint64_t instead of hashing the whole vector<Item>.  Frontiers are owned
    // by unique_ptr so their addresses are stable — solve() returns const& into
    // the cache, eliminating per-hit copies.
    //
    // For n ≤ FLAT_CACHE_MAX_BITS the cache is a flat array indexed by the
    // mask: reads are a single atomic load (lock-free, no contention).  Larger
    // n falls back to the unordered_map + mutex.
    static constexpr size_t FLAT_CACHE_MAX_BITS = 20;
    bool _using_flat = false;                              // set per pass
    std::unique_ptr<std::atomic<Frontier*>[]> _flat_cache; // lock-free slots (2^n)
    size_t _flat_capacity = 0;                             // slots currently allocated
    std::vector<std::unique_ptr<Frontier>> _owners;        // stable storage (pass lifetime)
    std::mutex _owners_mutex;                              // guards _owners push (flat path)
    std::unordered_map<uint64_t, std::unique_ptr<Frontier>> _cache;  // fallback (n > 20)
    /// Stable-address arena for frontiers when _cache hits MAX_CACHE_ENTRIES.
    /// Entries are never freed during a solve (lifetime = the pass).
    std::vector<std::unique_ptr<Frontier>> _overflow;
    mutable std::shared_mutex _cache_mutex;

    // Steps of the deterministic hamming-style construction (filled by
    // compute_ub); used as an anytime fallback when the exact search gives up.
    std::vector<EnchStep> _ub_steps;

    /// Best complete-solution cost found so far (anytime bound).  Shared
    /// across the parallel top-level; read as the B&B bound by all prunes.
    std::atomic<int32_t> _best_cost{INT32_MAX};

    // ── Diagnostics (PartitionDpDiagnostics, spec Tier 0/1) ─────────────
    // Per-pass aggregates; flushed into _diag at pass end.  Tier 1 counters
    // are incremented once per subproblem (≤ 2^n), never per operation.
    std::atomic<uint64_t> _dp_cap_pruned{0};
    std::atomic<uint64_t> _dp_bound_pruned{0};
    std::atomic<uint64_t> _dp_pareto_dropped{0};

    PartitionDpDiagnostics _diag;

    const Frontier& solve(uint64_t mask, int32_t max_step_cost,
                          int32_t beam_width, bool parallelize,
                          bool final_level, ExecutionContext &ctx);
    /// Lock-free lookup for the flat cache (n ≤ FLAT_CACHE_MAX_BITS).
    /// Returns nullptr on miss.  Falls back to the mutex map for large n.
    const Frontier* cache_get(uint64_t mask) const noexcept;
    /// Publish \p f for \p mask (flat: CAS; map: insert under mutex).  Returns
    /// a reference to the stored frontier — ours, or a concurrent winner's
    /// (equivalent).  Storage lives for the whole pass.
    const Frontier& cache_put(uint64_t mask, std::unique_ptr<Frontier> f);
    /// (Re)build the memo cache for a problem of \p n items: use the lock-free
    /// flat array when n ≤ FLAT_CACHE_MAX_BITS, else the mutex map.
    void _prepare_cache(size_t n);
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
