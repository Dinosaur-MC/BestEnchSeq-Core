#include "AlgorithmDiagnostics.h"
namespace algorithm {

// ─── AlgorithmDiagnostics ─────────────────────────────────────────────
//
// Note: status, wall_ms, and algorithm are written by DiagnosticsWriter
// from its function parameters — flush() reports only fields that are
// NOT already covered by the generic write() signature.

void AlgorithmDiagnostics::flush(std::vector<DiagnosticsWriter::Entry>& out) const {
    out.push_back({"status",          status});
    out.push_back({"solution_cost",   static_cast<int64_t>(solution_cost)});
    out.push_back({"diag_schema_version", static_cast<int64_t>(diag_schema_version)});
    out.push_back({"normalized_explored_states", normalized_explored_states});
}

// ─── SearchDiagnostics ────────────────────────────────────────────────

void SearchDiagnostics::flush(std::vector<DiagnosticsWriter::Entry>& out) const {
    AlgorithmDiagnostics::flush(out);
    out.push_back({"initial_bound",   static_cast<int64_t>(initial_bound)});
    out.push_back({"final_bound",     static_cast<int64_t>(final_bound)});
    // solutions_found / max_depth are emitted only when the algorithm reports
    // them (>= 0); -1 means "not tracked / not applicable" (e.g. DP strategies).
    if (solutions_found >= 0)
        out.push_back({"solutions_found", static_cast<int64_t>(solutions_found)});
    if (max_depth_reached >= 0)
        out.push_back({"max_depth",       static_cast<int64_t>(max_depth_reached)});
}

// ─── PoolSearchDiagnostics ────────────────────────────────────────────

void PoolSearchDiagnostics::flush(std::vector<DiagnosticsWriter::Entry>& out) const {
    SearchDiagnostics::flush(out);
    out.push_back({"items_pool_used",      static_cast<int64_t>(items_pool_used)});
    out.push_back({"items_pool_capacity",  static_cast<int64_t>(items_pool_capacity)});
    out.push_back({"step_pool_used",       static_cast<int64_t>(step_pool_used)});
    out.push_back({"step_pool_capacity",   static_cast<int64_t>(step_pool_capacity)});
}

// ─── PartitionDpDiagnostics ─────────────────────────────────────────────
// Template per docs/algotithm_designs/algorithm-diagnostics-spec.md §8.
// Field names carry the `dp_` paradigm prefix (spec §6).

void PartitionDpDiagnostics::flush(std::vector<DiagnosticsWriter::Entry>& out) const {
    SearchDiagnostics::flush(out);
    out.push_back({"dp_subproblems_solved", static_cast<int64_t>(dp_subproblems_solved)});
    out.push_back({"dp_cache_slots",        static_cast<int64_t>(dp_cache_slots)});
    out.push_back({"dp_cache_hits",         static_cast<int64_t>(dp_cache_hits)});
    out.push_back({"dp_max_frontier_size",  static_cast<int64_t>(dp_max_frontier_size)});
    out.push_back({"dp_cap_pruned",         static_cast<int64_t>(dp_cap_pruned)});
    out.push_back({"dp_bound_pruned",       static_cast<int64_t>(dp_bound_pruned)});
    out.push_back({"dp_pareto_dropped",     static_cast<int64_t>(dp_pareto_dropped)});
    out.push_back({"dp_ub_cost",            static_cast<int64_t>(dp_ub_cost)});
    // dp_pass_b_ran reports "did the unconstrained Pass B run" — a false is the
    // default/disabled state and carries no information, so emit only on true.
    if (dp_pass_b_ran)
        out.push_back({"dp_pass_b_ran", 1});
}

} // namespace algorithm
