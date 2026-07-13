#pragma once
#include "types/CompactedTypes.h"
#include "registries/CompactedRegistries.h"
#include "types/ForgeConfig.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// ─── Search configuration ─────────────────────────────────────────
struct SearchConfig {
    int32_t max_solutions = 0;
    int32_t max_depth = 0;
    int32_t memory_mb = 0;
    std::chrono::milliseconds max_search_time{0};
};

// ─── Algorithm input ───
struct AlgorithmInput {
    ForgeConfig config;              // forge configuration (platform, flags)
    SearchConfig search;             // search configuration (solutions, depth, time)
    compact::ItemCollection items;   // items[0] = equipment, rest = books
    compact::EnchCollection target;  // desired final enchantments
    compact::EnchReg ench_reg;       // compact registry (must be initialized)
};

// ─── Algorithm output (compact steps) ───
struct AlgorithmOutput {
    std::string algorithm_name;
    std::string algorithm_version;
    std::chrono::system_clock::time_point created_at;
    std::chrono::milliseconds computation_time{0};
    std::vector<std::vector<compact::EnchStep>> steps;
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

    /// Apply forge configuration before execute().
    /// Called by AlgorithmExecutor::start() with AlgorithmInput::config.
    /// Default no-op; strategies override to forward to their ForgeEngine.
    virtual void configure(const ForgeConfig &cfg) noexcept { (void)cfg; }

    virtual bool is_resumable() const noexcept { return false; }
    virtual std::vector<uint8_t> serialize_state() const { return {}; }
    virtual void deserialize_state(const std::vector<uint8_t> &) {}
};
