#include "algorithm/strategies/astar/AStarDiagnostics.h"
#include "algorithm/ExecutionContext.h"

void AStarDiagnostics::flush(ExecutionContext& ctx) const {
    PoolSearchDiagnostics::flush(ctx);
    ctx.report_diagnostic("explored_count",       explored_count);
    ctx.report_diagnostic("best_g_entries",       static_cast<int64_t>(best_g_entries));
    ctx.report_diagnostic("open_set_pending",     static_cast<int64_t>(open_set_pending));
    ctx.report_diagnostic("pruned_by_cost",       pruned_by_cost);
    ctx.report_diagnostic("pruned_by_best_g",     pruned_by_best_g);
    ctx.report_diagnostic("pruned_by_f",          pruned_by_f);
    ctx.report_diagnostic("pruned_by_caps",       pruned_by_caps);
    ctx.report_diagnostic("estimated_peak_bytes", estimated_peak_bytes);
}
