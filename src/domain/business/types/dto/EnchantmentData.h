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
    std::vector<std::string> exclusive_with; ///< Conflicting enchantment IDs
    std::vector<std::string> applicable_to;  ///< 原始 supported_items 引用（`#tag` 或具体物品 ID，透传不展开）
};

} // namespace business::loader
