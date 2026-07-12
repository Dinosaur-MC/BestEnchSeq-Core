#pragma once
#include <cstdint>
#include <cstddef>

/// Generic algorithm diagnostics snapshot for on-exit logging.
///
/// Designed for algorithms that don't need the full AStarDiagnostics
/// (DFS, Greedy, DPB, HMS).  Writes a timestamped file to logs/diag/
/// when write() is called.
///
/// Compiled out entirely when BESQ_DISABLE_DIAGNOSTICS is defined.
/// (The macro name is historical — it applies to all algorithm diagnostics.)
struct AlgorithmDiagnostics {
    int64_t  nodes_visited = 0;
    int64_t  nodes_pruned  = 0;
    int64_t  steps_forged  = 0;
    int32_t  solution_cost = -1;
    int64_t  wall_ms       = 0;
    const char* label      = "";
    const char* status     = "";

#ifndef BESQ_DISABLE_DIAGNOSTICS
    void write() const;
#else
    void write() const {}  // no-op
#endif
};
