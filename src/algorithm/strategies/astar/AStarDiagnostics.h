#pragma once
#include <cstdint>
#include <cstddef>

/// Diagnostics snapshot for AStar exit logging.
///
/// Pure-data struct.  Use DiagnosticsWriter::write() to persist to disk.
struct AStarDiagnostics {
    int64_t  explored_count      = 0;
    size_t   best_g_size         = 0;
    size_t   step_pool_used      = 0;
    size_t   step_pool_capacity  = 0;
    size_t   items_pool_size     = 0;
    size_t   items_pool_capacity = 0;
    size_t   open_set_pending    = 0;
    int64_t  pruned_by_cost      = 0;
    int64_t  pruned_by_best_g    = 0;
    int64_t  pruned_by_f         = 0;
    int64_t  pruned_by_caps      = 0;
    int64_t  steps_forged        = 0;
    int32_t  solution_cost       = -1;
    int64_t  wall_ms             = 0;
    int64_t  estimated_peak_bytes = 0;
    const char* status           = "";
};
