#pragma once
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"
#include <cstdint>
#include <cstddef>

/// AStar-specific diagnostics, extending the pool-based search hierarchy.
namespace algorithm {
struct AStarDiagnostics : PoolSearchDiagnostics {
    int64_t  explored_count       = 0;
    size_t   best_g_entries       = 0;
    size_t   open_set_pending     = 0;
    int64_t  pruned_by_cost       = 0;
    int64_t  pruned_by_best_g     = 0;
    int64_t  pruned_by_f          = 0;
    int64_t  pruned_by_caps       = 0;
    int64_t  estimated_peak_bytes = 0;

    void flush(std::vector<DiagnosticsWriter::Entry>& out) const override;
};
} // namespace algorithm
