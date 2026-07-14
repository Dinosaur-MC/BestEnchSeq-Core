#pragma once
#include <cstdint>
#include <string>

class ExecutionContext;

/// ─── Base class for all algorithm diagnostics ──────────────────────────
///
/// Each algorithm keeps a private instance and calls flush() on exit.
/// flush() converts all fields to KV pairs via ctx.report_diagnostic().
/// Executor::_finalize() collects the KV pairs and pushes them to the
/// global DiagnosticsService for async file persistence.
struct AlgorithmDiagnostics {
    std::string algorithm_name;
    std::string status;            // "Complete", "Cancelled", etc.
    int32_t     solution_cost{-1};

    virtual ~AlgorithmDiagnostics() = default;

    /// Flatten all fields to KV pairs and report via ctx.
    /// Derived classes must call the parent's flush() first.
    virtual void flush(ExecutionContext& ctx) const;
};

/// ─── Search algorithms (expand state nodes) ────────────────────────────
///
/// For AStar, IDAStar, DFS — algorithms that explore a search space
/// by expanding nodes and tracking bounds.
struct SearchDiagnostics : AlgorithmDiagnostics {
    int32_t initial_bound{INT32_MAX};
    int32_t final_bound{INT32_MAX};
    int32_t solutions_found{0};
    int32_t max_depth_reached{0};

    void flush(ExecutionContext& ctx) const override;
};

/// ─── Pool-based search (ItemPool) ──────────────────────────────────────
///
/// For AStar, IDAStar — algorithms that use ItemPool + ItemID for state.
struct PoolSearchDiagnostics : SearchDiagnostics {
    size_t items_pool_used{0};
    size_t items_pool_capacity{0};
    size_t step_pool_used{0};
    size_t step_pool_capacity{0};

    void flush(ExecutionContext& ctx) const override;
};
