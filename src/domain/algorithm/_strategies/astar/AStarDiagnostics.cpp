#include "AStarDiagnostics.h"
namespace algorithm {

void AStarDiagnostics::flush(std::vector<DiagnosticsWriter::Entry>& out) const {
    PoolSearchDiagnostics::flush(out);
    out.push_back({"explored_count",       explored_count});
    out.push_back({"best_g_entries",       static_cast<int64_t>(best_g_entries)});
    out.push_back({"open_set_pending",     static_cast<int64_t>(open_set_pending)});
    out.push_back({"pruned_by_cost",       pruned_by_cost});
    out.push_back({"pruned_by_best_g",     pruned_by_best_g});
    out.push_back({"pruned_by_f",          pruned_by_f});
    out.push_back({"pruned_by_caps",       pruned_by_caps});
    out.push_back({"estimated_peak_bytes", estimated_peak_bytes});
}

} // namespace algorithm
