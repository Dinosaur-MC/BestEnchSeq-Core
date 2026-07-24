#pragma once
#include <string>
#include <vector>

// ====================================================================
// BestEnchSeq — Interface Domain: Shared CLI/Spec Types
// ====================================================================
// Pure data types produced by parsers and consumed by CLI and
// registry-resolution helpers.  No business-domain dependencies.
// No registry dependencies.

/// A single enchantment spec parsed from CLI input.
///
/// "sharpness=5"  → ns="minecraft", id="sharpness", level=5
/// "mod:id=3"     → ns="mod", id="id", level=3
struct EnchantmentSpec {
    std::string ns = "minecraft";
    std::string id;
    int level = 1;
};

/// An equipment target spec parsed from CLI input.
///
/// "diamond_sword[sharpness=5,unbreaking=3]"
///   → item_id="diamond_sword", inline_enchants = {sharpness=5, unbreaking=3}
struct TargetSpec {
    std::string item_id;
    std::vector<EnchantmentSpec> inline_enchants;
};
