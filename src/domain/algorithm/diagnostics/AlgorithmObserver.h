#pragma once
#include "domain/algorithm/types/AlgorithmState.h"
#include "domain/algorithm/diagnostics/ProgressStatus.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/types/Solution.h"
#include <vector>

namespace algorithm {

/// Simple diagnostic message.
struct DiagnosticInfo {
    std::string message;
};

// ─── Observer (streaming callbacks, compact-only) ───
class AlgorithmObserver {
  public:
    virtual ~AlgorithmObserver() = default;

    /// Return false to suppress all callbacks for the given task_id.
    /// Default: accept all tasks.
    virtual bool accept_task_id(size_t) const { return true; }

    virtual void on_progress(size_t task_id, uint8_t pct, ProgressStatus status) {}
    virtual void on_solution_found(size_t task_id, const std::vector<EnchStep> &solution) {}
    virtual void on_state_changed(size_t task_id, AlgorithmState prev, AlgorithmState curr) {}
    virtual void on_diagnostic(size_t task_id, const DiagnosticInfo &info) {}
    virtual void on_completed(size_t task_id, const AlgorithmOutput &output) {}
};

} // namespace algorithm
