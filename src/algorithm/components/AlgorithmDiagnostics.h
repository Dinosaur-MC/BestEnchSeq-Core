#pragma once
#include <cstdint>

/// Generic algorithm diagnostics snapshot for on-exit logging.
///
/// Pure-data struct for algorithms that don't need the full
/// AStarDiagnostics (DFS, Greedy, DPB, HierarchicalMerge).
/// Use DiagnosticsWriter::write() to persist to disk.
struct AlgorithmDiagnostics {
    int64_t  nodes_visited = 0;
    int64_t  nodes_pruned  = 0;
    int64_t  steps_forged  = 0;
    int32_t  solution_cost = -1;
    int64_t  wall_ms       = 0;
    const char* label      = "";
    const char* status     = "";
};
