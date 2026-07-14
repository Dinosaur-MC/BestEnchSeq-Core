#pragma once
#include "types/AlgorithmTypes.h"
#include <vector>

// ─── Observer (streaming callbacks, compact-only) ───
class AlgorithmObserver {
public:
    virtual ~AlgorithmObserver() = default;

    virtual void on_progress(double percent, ProgressStatus status) {}
    virtual void on_solution_found(const std::vector<compact::EnchStep>& solution) {}
    virtual void on_state_changed(AlgorithmState prev, AlgorithmState curr) {}
    virtual void on_diagnostic(const DiagnosticInfo& info) {}
    virtual void on_completed(const AlgorithmOutput& output) {}
};
