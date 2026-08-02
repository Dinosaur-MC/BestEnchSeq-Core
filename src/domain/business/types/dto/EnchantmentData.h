#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace business::loader {

/// Data transfer object for enchantment data produced by parsers.
///
/// Represents an enchantment definition in its raw (pre-resolution) form,
/// using string identifiers for cross-references. The RegistryLoader
/// resolves these strings to NSID-based business registry types.
struct EnchantmentData {
    std::string id;                          ///< "minecraft:sharpness"
    std::string display_name;
    int32_t multiplier       = 0;
    int32_t max_level        = 0;
    int32_t limited_level    = 0;            ///< 0 = treasure
    bool limited_level_provided = false;     ///< 数据中提供了 limited_level 字段（旧格式预计算值）
    std::vector<std::string> exclusive_with; ///< Conflicting enchantment IDs
    std::vector<std::string> applicable_to;  ///< 原始 supported_items 引用（`#tag` 或具体物品 ID，透传不展开）
    int32_t min_cost_base      = 0;          ///< min_cost.base（附魔台成本公式）
    int32_t min_cost_per_level = 0;          ///< min_cost.per_level_above_first
};

} // namespace business::loader
