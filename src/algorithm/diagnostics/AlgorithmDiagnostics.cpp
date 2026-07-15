#include "algorithm/diagnostics/AlgorithmDiagnostics.h"

// ─── AlgorithmDiagnostics ─────────────────────────────────────────────
//
// Note: status, wall_ms, and algorithm are written by DiagnosticsWriter
// from its function parameters — flush() reports only fields that are
// NOT already covered by the generic write() signature.

void AlgorithmDiagnostics::flush(std::vector<DiagnosticsWriter::Entry>& out) const {
    out.push_back({"status",        status});
    out.push_back({"solution_cost", static_cast<int64_t>(solution_cost)});
}

// ─── SearchDiagnostics ────────────────────────────────────────────────

void SearchDiagnostics::flush(std::vector<DiagnosticsWriter::Entry>& out) const {
    AlgorithmDiagnostics::flush(out);
    out.push_back({"initial_bound",   static_cast<int64_t>(initial_bound)});
    out.push_back({"final_bound",     static_cast<int64_t>(final_bound)});
    out.push_back({"solutions_found", static_cast<int64_t>(solutions_found)});
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
