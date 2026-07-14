#pragma once

// ─── Unified header: includes all strategy algorithm headers ──────────
//
// Consumers (main.cpp, forge_benchmark.cpp, tests) include this single
// header instead of knowing the per-strategy subdirectory layout.

#include "algorithm/strategies/astar/AStarAlgorithm.h"                             // IWYU pragma: export
#include "algorithm/strategies/dfs/DFSAlgorithm.h"                                 // IWYU pragma: export
#include "algorithm/strategies/diff_first/DiffFirstAlgorithm.h"                    // IWYU pragma: export
#include "algorithm/strategies/greedy/GreedyAlgorithm.h"                           // IWYU pragma: export
#include "algorithm/strategies/hamming/HammingAlgorithm.h"                         // IWYU pragma: export
#include "algorithm/strategies/hierarchical/HierarchicalMergeAlgorithm.h"          // IWYU pragma: export
#include "algorithm/strategies/idastar/IDAStarAlgorithm.h"                         // IWYU pragma: export
#include "algorithm/strategies/penalty_balance/DynamicPenaltyBalancingAlgorithm.h" // IWYU pragma: export
