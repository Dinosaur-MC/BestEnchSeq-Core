#pragma once
#include "common/io/ISerializable.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/types/Item.h"
#include "domain/algorithm/types/Solution.h"
#include <chrono>
#include <variant>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace algorithm {

/// Operation mode.
enum class AlgorithmMode : uint8_t {
    direct,
    inventory,
};

// ─── Source data — tagged by AlgorithmMode ───────────────────────────────
// Direct mode: EnchCollection = current enchantments on the equipment.
// Inventory mode: ItemCollection = available items pool.
using SourceData = std::variant<EnchCollection, ItemCollection>;

// ─── Algorithm input ───
struct AlgorithmInput : ISerializable {
    ForgeConfig f_config;                    // forge configuration (platform, flags)
    SearchConfig s_config;                   // search configuration (solutions, mode)
    EnchReg ench_reg;                        // compact registry (must be initialized)
    Item target;                             // target item with wanted enchantments
    AlgorithmMode mode = AlgorithmMode::direct;
    SourceData data;                         // source (direct) or available items (inventory)
    std::vector<int32_t> priorities;         // priority per item (inventory mode)

    // Flattened execution view — populated by pipeline before execute().
    // items[0] = equipment (target with source enchants), rest = books/extra.
    ItemCollection items;
    int32_t initial_bound = INT32_MAX;       // warm-start bound

    bool is_direct() const noexcept { return mode == AlgorithmMode::direct; }
    bool is_inventory() const noexcept { return mode == AlgorithmMode::inventory; }

    const EnchCollection& source() const noexcept {
        return std::get<EnchCollection>(data);
    }
    EnchCollection& source() noexcept {
        return std::get<EnchCollection>(data);
    }
    const ItemCollection& inventory_items() const noexcept {
        return std::get<ItemCollection>(data);
    }
    ItemCollection& inventory_items() noexcept {
        return std::get<ItemCollection>(data);
    }

    void serialize(ByteStreamWriter &w) const noexcept override {
        w << f_config << s_config << ench_reg << target << static_cast<uint8_t>(mode);
        if (is_direct())
            w << source();
        else
            w << inventory_items();
        w << priorities << items << initial_bound;
    }
    void deserialize(ByteStreamReader &r) noexcept override {
        uint8_t m;
        r >> f_config >> s_config >> ench_reg >> target >> m;
        mode = static_cast<AlgorithmMode>(m);
        if (is_direct()) {
            EnchCollection src;
            r >> src;
            data = std::move(src);
        } else {
            ItemCollection its;
            r >> its;
            data = std::move(its);
        }
        r >> priorities >> items >> initial_bound;
    }
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

}; // namespace algorithm
