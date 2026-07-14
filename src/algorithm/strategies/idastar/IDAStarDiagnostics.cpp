#include "algorithm/strategies/idastar/IDAStarDiagnostics.h"
#include "algorithm/ExecutionContext.h"

void IDAStarDiagnostics::flush(ExecutionContext& ctx) const {
    PoolSearchDiagnostics::flush(ctx);
    ctx.report_diagnostic("tt_lookups",        static_cast<int64_t>(tt_lookups));
    ctx.report_diagnostic("tt_stores",         static_cast<int64_t>(tt_stores));
    ctx.report_diagnostic("solution_path_len", static_cast<int64_t>(solution_path_len));
}
