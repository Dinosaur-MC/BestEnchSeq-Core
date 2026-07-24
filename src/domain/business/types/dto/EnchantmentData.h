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
    std::vector<std::string> applicable_to;  ///< Applicable equipment category names
};

} // namespace business::loader
