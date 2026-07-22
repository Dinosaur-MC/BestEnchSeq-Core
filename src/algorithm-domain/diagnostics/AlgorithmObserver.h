#pragma once
#include "../types/AlgorithmTypes.h"
#include <vector>

namespace algorithm {

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
