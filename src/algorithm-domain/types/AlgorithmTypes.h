#pragma once
#include "../registries/EnchReg.h"
#include "../types/ConfigTypes.h"
#include "../types/Item.h"
#include "../types/Solution.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/// Operation mode: direct (given source enchants + target) or inventory
/// (using items parsed from an inventory file).  Bitmask — an algorithm
/// may support one or both modes.
enum class AlgorithmMode : uint8_t {
    direct    = 1 << 0,
    inventory = 1 << 1,
};

constexpr AlgorithmMode operator|(AlgorithmMode a, AlgorithmMode b) noexcept {
    return static_cast<AlgorithmMode>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr bool operator&(AlgorithmMode a, AlgorithmMode b) noexcept {
    return static_cast<uint8_t>(a) & static_cast<uint8_t>(b);
}

// ─── Algorithm input ───
struct AlgorithmInput {
    ForgeConfig config;                         // forge configuration (platform, flags)
    SearchConfig search;                        // search configuration (solutions, depth, time)
    AlgorithmMode mode = AlgorithmMode::direct; // operation mode
    ItemCollection items;                       // items[0] = equipment, rest = books
    EnchCollection target;                      // desired final enchantments
    EnchReg ench_reg;                           // compact registry (must be initialized)
    int32_t initial_bound = INT32_MAX;          // warm-start: skip own bound if tighter
};

// ─── Algorithm output (compact solutions) ───
struct AlgorithmOutput {
    std::string algorithm_name;
    std::string algorithm_version;
    std::chrono::system_clock::time_point created_at;
    std::chrono::milliseconds computation_time{0};
    std::vector<EnchSolution> solutions;
    size_t task_id{0};
    Item final_item;
    bool is_valid = false;
};

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
