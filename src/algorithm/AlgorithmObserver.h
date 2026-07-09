#pragma once
#include "types/CompactedTypes.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Forward declaration (full definition in IAlgorithm.h)
struct AlgorithmOutput;

// ─── Progress status enum ───
enum class ProgressStatus : uint8_t {
    Starting = 0,
    ApplyingSacrifice,
    Exploring,
    MergingWithinGroups,
    MergingGroups,
    ApplyingToEquipment,
    GoalAlreadyMet,
    Complete,
    CompleteNoSolution,
    Cancelled,
};

inline constexpr std::string_view to_string(ProgressStatus s) noexcept {
    switch (s) {
        case ProgressStatus::Starting:           return "starting";
        case ProgressStatus::ApplyingSacrifice:  return "applying sacrifice";
        case ProgressStatus::Exploring:          return "exploring";
        case ProgressStatus::MergingWithinGroups: return "merging within groups";
        case ProgressStatus::MergingGroups:       return "merging groups";
        case ProgressStatus::ApplyingToEquipment: return "applying to equipment";
        case ProgressStatus::GoalAlreadyMet:     return "goal already met";
        case ProgressStatus::Complete:           return "complete";
        case ProgressStatus::CompleteNoSolution: return "no solution found";
        case ProgressStatus::Cancelled:          return "cancelled";
    }
    return "unknown";
}

// ─── Diagnostic info ───
struct DiagnosticInfo {
    std::string message;
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
