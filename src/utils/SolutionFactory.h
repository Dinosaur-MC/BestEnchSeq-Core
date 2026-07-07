#pragma once
#include "../types/EnchSolution.h"
#include <string>
#include <vector>

// Forward declarations
class IAlgorithm;
struct AlgorithmOutput;

class SolutionFactory {
public:
    // 从完整输出创建一组方案 (post-execution)
    static std::vector<EnchSolution> create(
        platform::MCE platform,
        const EnchSet& original_ench,
        const ItemStack& target_item,
        const ItemCollection& available_items,
        const AlgorithmOutput& output
    );

    // 从流式中间结果创建单条方案 (per-solution callback)
    static EnchSolution create_single(
        platform::MCE platform,
        const EnchSet& original_ench,
        const ItemStack& target_item,
        const ItemCollection& available_items,
        const EnchStepList& steps,
        std::string_view algo_name,
        std::string_view algo_version,
        bool is_valid = true
    );
};
