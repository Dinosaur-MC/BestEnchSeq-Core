#pragma once
#include <cstdint>
#include <string_view>

namespace algorithm {

/// Progress status reported during algorithm execution.
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
    case ProgressStatus::Starting:
        return "starting";
    case ProgressStatus::ApplyingSacrifice:
        return "applying sacrifice";
    case ProgressStatus::Exploring:
        return "exploring";
    case ProgressStatus::MergingWithinGroups:
        return "merging within groups";
    case ProgressStatus::MergingGroups:
        return "merging groups";
    case ProgressStatus::ApplyingToEquipment:
        return "applying to equipment";
    case ProgressStatus::GoalAlreadyMet:
        return "goal already met";
    case ProgressStatus::Complete:
        return "complete";
    case ProgressStatus::CompleteNoSolution:
        return "no solution found";
    case ProgressStatus::Cancelled:
        return "cancelled";
    }
    return "unknown";
}

} // namespace algorithm
