#pragma once
#include "../BESQTypes.h"
#include <string>
#include <string_view>

// Forward declaration (full definition in IAlgorithm.h)
struct AlgorithmOutput;

// ─── Diagnostic info (placeholder for now) ───
struct DiagnosticInfo {
    std::string message;
    // Extended fields TBD in phase 2
};

// ─── Algorithm state machine ───
enum class AlgorithmState {
    Idle,
    Running,
    Paused,
    Completed,
    Failed,
    Cancelled,
};

// ─── Observer (streaming callbacks) ───
class AlgorithmObserver {
public:
    virtual ~AlgorithmObserver() = default;

    virtual void on_progress(double percent, std::string_view status) {}
    virtual void on_solution_found(const EnchStepList& solution) {}
    virtual void on_state_changed(AlgorithmState prev, AlgorithmState curr) {}
    virtual void on_diagnostic(const DiagnosticInfo& info) {}
    virtual void on_completed(const AlgorithmOutput& output) {}
};
