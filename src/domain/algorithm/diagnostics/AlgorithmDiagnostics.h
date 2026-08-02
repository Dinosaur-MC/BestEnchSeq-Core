#pragma once
#include "DiagnosticsWriter.h"
#include <cstdint>
#include <string>
#include <vector>

/// ─── Base class for all algorithm diagnostics ──────────────────────────
///
/// Each algorithm keeps a private instance and calls flush() on exit.
/// flush() converts all fields to KV pairs and pushes them into out.
/// Executor::_finalize() collects the KV pairs and pushes them to the
/// global DiagnosticsService for async file persistence.
///
/// SPEC: the full diagnostics contract for built-in AND plugin algorithms is
/// documented in `docs/algotithm_designs/algorithm-diagnostics-spec.md`
/// (field naming, performance tiers, taxonomy).  Every algorithm must fill
/// the common core (§4): status / solution_cost / normalized_explored_states.
namespace algorithm {
struct AlgorithmDiagnostics {
    std::string algorithm_name;
    std::string status;            // "Complete", "Cancelled", etc.
    int32_t     solution_cost{-1};
    /// Schema version of this diagnostics struct.  Field names are stable
    /// once published; consumers key compatibility off this.
    int32_t     diag_schema_version{1};
    /// Cross-algorithm comparable "how much search did you do": search
    /// strategies set explored_count, DP strategies set subproblems_solved.
    /// -1 = not reported.
    int64_t     normalized_explored_states{-1};

    virtual ~AlgorithmDiagnostics() = default;

    /// Flatten all fields to KV pairs and push into out.
    /// Derived classes must call the parent's flush() first.
    virtual void flush(std::vector<DiagnosticsWriter::Entry>& out) const;
};

/// ─── Search algorithms (expand state nodes) ────────────────────────────
///
/// For AStar, IDAStar, DFS — algorithms that explore a search space
/// by expanding nodes and tracking bounds.
struct SearchDiagnostics : AlgorithmDiagnostics {
    int32_t initial_bound{INT32_MAX};
    int32_t final_bound{INT32_MAX};
    int32_t solutions_found{0};
    int32_t max_depth_reached{0};

    void flush(std::vector<DiagnosticsWriter::Entry>& out) const override;
};

/// ─── Pool-based search (ItemPool) ──────────────────────────────────────
///
/// For AStar, IDAStar — algorithms that use ItemPool + ItemID for state.
struct PoolSearchDiagnostics : SearchDiagnostics {
    size_t items_pool_used{0};
    size_t items_pool_capacity{0};
    size_t step_pool_used{0};
    size_t step_pool_capacity{0};

    void flush(std::vector<DiagnosticsWriter::Entry>& out) const override;
};

/// ─── Partition DP (Catalan divide-and-conquer) ─────────────────────────
///
/// For bb_dp, dp_merge and any future DP plugin.  Template per
/// `docs/algotithm_designs/algorithm-diagnostics-spec.md` §8.
///
/// Performance tiers:
///   - Tier 0 (zero-cost, derived at pass end by scanning the memo cache):
///     dp_subproblems_solved / dp_cache_slots / dp_cache_hits /
///     dp_max_frontier_size / dp_ub_cost / dp_pass_b_ran.
///   - Tier 1 (one relaxed atomic per subproblem, ≤ 2^n):
///     dp_cap_pruned / dp_bound_pruned / dp_pareto_dropped — accumulate
///     locally inside one subproblem, flush to the global once at its end.
///   - There is NO Tier 2 here by default: per-forge counters are forbidden
///     unless gated behind BESQ_DEEP_DIAGNOSTICS (see spec §5).
struct PartitionDpDiagnostics : SearchDiagnostics {
    uint64_t dp_subproblems_solved{0};   // memo misses (real computations)
    uint64_t dp_cache_slots{0};          // flat-cache slots (1<<n) or map size
    uint64_t dp_cache_hits{0};           // flat path: slots - solved
    uint32_t dp_max_frontier_size{0};    // largest frontier across the search
    uint64_t dp_cap_pruned{0};           // forges rejected by max_step_cost
    uint64_t dp_bound_pruned{0};         // combines rejected by the B&B bound
    uint64_t dp_pareto_dropped{0};       // entries dropped by Pareto domination
    int32_t  dp_ub_cost{INT32_MAX};      // initial upper-bound cost (compute_ub)
    bool     dp_pass_b_ran{false};       // unconstrained Pass B ran

    void flush(std::vector<DiagnosticsWriter::Entry>& out) const override;
};
} // namespace algorithm
