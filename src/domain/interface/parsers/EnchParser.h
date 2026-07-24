#pragma once
#include <string>
#include <vector>

#include "domain/interface/types/SpecTypes.h"

/// Parse enchantment spec strings from CLI input.
///
/// Supported formats:
///   "sharpness=5"              -> ns=minecraft, id=sharpness, level=5
///   "minecraft:sharpness=5"    -> ns=minecraft, id=sharpness, level=5
///   "sharpness:5"              -> colon shorthand (all digits after colon)
///   "sharpness"                -> level defaults to 1
///
/// Multiple specs are comma-separated: "sharpness=5,knockback=2"
///
/// Throws std::runtime_error on:
///   - Level < 1 or > 255
///   - Empty enchantment id
struct EnchParser {
    static std::vector<EnchantmentSpec> parse(const std::string &input);
};
