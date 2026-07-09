#pragma once
#include "types/CompactedTypes.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace compact { class EnchReg; }

// ─── Algorithm output (produced by Executor after execution) ───
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

    // Execute with pure compact types — zero domain dependencies.
    virtual void execute(
        const std::vector<compact::Item>& items,
        const compact::EnchReg& reg,
        const std::vector<compact::Ench>& target,
        ExecutionContext& ctx
    ) = 0;

    // Serialization support (optional, resumable algorithms only)
    virtual bool is_resumable() const noexcept { return false; }
    virtual std::vector<uint8_t> serialize_state() const { return {}; }
    virtual void deserialize_state(const std::vector<uint8_t>&) {}
};
