#pragma once
#include "types/CompactedTypes.h"
#include "registries/CompactedRegistries.h"
#include "config/ForgeConfig.h"
#include "config/SearchConfig.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// ─── Algorithm input ───
struct AlgorithmInput {
    ForgeConfig config;              // forge configuration (platform, flags)
    SearchConfig search;             // search configuration (solutions, depth, time)
    compact::ItemCollection items;   // items[0] = equipment, rest = books
    compact::EnchCollection target;  // desired final enchantments
    compact::EnchReg ench_reg;       // compact registry (must be initialized)
};

// ─── Algorithm output (compact solutions) ───
struct AlgorithmOutput {
    std::string algorithm_name;
    std::string algorithm_version;
    std::chrono::system_clock::time_point created_at;
    std::chrono::milliseconds computation_time{0};
    std::vector<compact::EnchSolution> solutions;
    compact::Item final_item;
    bool is_valid = false;
};

class ExecutionContext;

// ─── IAlgorithm (pure interface, compact-only) ───
class IAlgorithm {
  public:
    virtual ~IAlgorithm() = default;

    virtual std::string_view name() const noexcept = 0;
    virtual std::string_view version() const noexcept = 0;

    virtual void execute(const AlgorithmInput &input, ExecutionContext &ctx) = 0;

    /// Quick feasibility check: returns true if the target is reachable
    /// from the given items without computing exact costs.
    /// Default returns true (pessimistic).  Strategies that implement this
    /// provide fast pre-filtering for inventory mode.
    virtual bool simulate(const AlgorithmInput &input) const noexcept { (void)input; return true; }

    virtual bool is_resumable() const noexcept { return false; }
    virtual std::vector<uint8_t> serialize_state() const { return {}; }
    virtual void deserialize_state(const std::vector<uint8_t> &) {}
};
