#include "algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "algorithm/ExecutionContext.h"

// ─── AlgorithmDiagnostics ─────────────────────────────────────────────
//
// Note: status, wall_ms, and algorithm are written by DiagnosticsWriter
// from its function parameters — flush() reports only fields that are
// NOT already covered by the generic write() signature.

void AlgorithmDiagnostics::flush(ExecutionContext& ctx) const {
    ctx.report_diagnostic("solution_cost", static_cast<int64_t>(solution_cost));
}

// ─── SearchDiagnostics ────────────────────────────────────────────────

void SearchDiagnostics::flush(ExecutionContext& ctx) const {
    AlgorithmDiagnostics::flush(ctx);
    ctx.report_diagnostic("initial_bound",   static_cast<int64_t>(initial_bound));
    ctx.report_diagnostic("final_bound",     static_cast<int64_t>(final_bound));
    ctx.report_diagnostic("solutions_found", static_cast<int64_t>(solutions_found));
    ctx.report_diagnostic("max_depth",       static_cast<int64_t>(max_depth_reached));
}

// ─── PoolSearchDiagnostics ────────────────────────────────────────────

void PoolSearchDiagnostics::flush(ExecutionContext& ctx) const {
    SearchDiagnostics::flush(ctx);
    ctx.report_diagnostic("items_pool_used",      static_cast<int64_t>(items_pool_used));
    ctx.report_diagnostic("items_pool_capacity",  static_cast<int64_t>(items_pool_capacity));
    ctx.report_diagnostic("step_pool_used",       static_cast<int64_t>(step_pool_used));
    ctx.report_diagnostic("step_pool_capacity",   static_cast<int64_t>(step_pool_capacity));
}
