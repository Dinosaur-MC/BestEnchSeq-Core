#pragma once
#include "algorithm/diagnostics/AlgorithmDiagnostics.h"
#include <cstddef>

/// IDAStar-specific diagnostics, extending the pool-based search hierarchy.
struct IDAStarDiagnostics : PoolSearchDiagnostics {
    size_t tt_lookups{0};
    size_t tt_stores{0};
    size_t solution_path_len{0};

    void flush(ExecutionContext& ctx) const override;
};
