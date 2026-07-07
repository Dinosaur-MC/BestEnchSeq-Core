#pragma once
#include "../BESQTypes.h"
#include "IForgeEngine.h"
#include "AlgorithmExecutor.h"
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// ─── Algorithm metadata ───
struct AlgorithmMetadata {
    std::string name;
    std::string version;
    std::chrono::system_clock::time_point created_at;
    std::chrono::milliseconds computation_time{0};
};

// ─── Algorithm output (produced by Executor after execution) ───
struct AlgorithmOutput {
    std::string algorithm_name;
    std::string algorithm_version;
    std::chrono::system_clock::time_point created_at;
    std::chrono::milliseconds computation_time{0};
    std::vector<EnchStepList> steps;
    bool is_valid = false;
};

// ─── Algorithm input ───
struct AlgorithmInput {
    platform::MCE platform;
    EnchSet original_ench;
    ItemStack target_item;
    ItemCollection available_items;
};

// ─── IAlgorithm (pure interface) ───
class IAlgorithm {
public:
    virtual ~IAlgorithm() = default;

    virtual std::string_view name() const noexcept = 0;
    virtual std::string_view version() const noexcept = 0;
    virtual const IForgeEngine& forge_engine() const noexcept = 0;

    // 核心同步执行算法
    virtual void execute(const AlgorithmInput& input, ExecutionContext& ctx) = 0;

    // 序列化支持（第二阶段）
    virtual bool is_resumable() const noexcept { return false; }
    virtual std::vector<uint8_t> serialize_state() const { return {}; }
    virtual void deserialize_state(const std::vector<uint8_t>&) {}
};
