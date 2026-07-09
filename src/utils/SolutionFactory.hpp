#pragma once
#include "../types/EnchSolution.h"
#include <string>
#include <string_view>
#include <vector>

class SolutionFactory {
public:
    // 从流式中间结果创建单条方案 (per-solution callback)
    static EnchSolution create_single(
        platform::MCE platform,
        const EnchSet& original_ench,
        const ItemStack& target_item,
        const ItemCollection& available_items,
        const EnchStepList& steps,
        std::string_view algo_name,
        std::string_view algo_version,
        bool is_valid = true)
    {
        return EnchSolution::make(
            platform, original_ench, target_item, available_items,
            steps, is_valid,
            EnchSolution::MetaData{
                std::string(algo_name),
                std::string(algo_version),
                0, 0
            }
        );
    }
};
