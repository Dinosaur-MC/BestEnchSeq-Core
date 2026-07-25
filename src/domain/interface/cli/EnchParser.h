#pragma once
#include "domain/business/types/EnchSet.h"
#include <string>

class EnchantmentRegistry;

/// Parse enchantment spec strings from CLI input into an EnchSet.
///
/// Supported formats:
///   "sharpness=5"              -> ns=minecraft, id=sharpness, level=5
///   "minecraft:sharpness=5"    -> ns=minecraft, id=sharpness, level=5
///   "sharpness:5"              -> colon shorthand (all digits after colon)
///   "sharpness"                -> level defaults to 1
///
/// Multiple specs are comma-separated: "sharpness=5,knockback=2"
///
/// Resolves enchantment names against the provided registry.
/// Throws std::runtime_error on:
///   - Unknown enchantment name
///   - Level < 1 or > 255
///   - Empty enchantment id
struct EnchParser {
    static EnchSet parse(const std::string &input,
                         const EnchantmentRegistry &ench_reg);
};
