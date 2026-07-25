#pragma once
#include "domain/business/types/Item.h"
#include <string>

class EnchantmentRegistry;
class EquipmentRegistry;

struct ItemParser {
    /// Parse target spec into an Item.
    ///
    /// Format (all parts optional except item_id):
    ///   <item_id>[<ench>=<level>,...]{<key>:<value>,...}
    ///
    /// Enchantment block [ ]:
    ///   "sharpness=5"              -> level shorthand
    ///   "sharpness:5"              -> colon shorthand
    ///   "minecraft:sharpness=5"    -> namespaced enchantment
    ///
    /// Properties block { }:
    ///   "prior_penalty:3"          -> anvil prior-work penalty (default 0)
    ///   "durability:500"           -> item durability (default 0)
    ///
    /// Examples:
    ///   "diamond_sword"
    ///   "diamond_sword[sharpness=5]"
    ///   "diamond_sword[sharpness=5]{prior_penalty:2}"
    ///   "diamond_sword[sharpness=5,knockback=2]{prior_penalty:3,durability:500}"
    ///
    /// Resolves equipment and enchantments against the given registries.
    /// Throws std::runtime_error on unknown equipment, unknown enchantments,
    /// malformed syntax, or unrecognised property keys.
    static Item parse(const std::string &input,
                      const EnchantmentRegistry &ench_reg,
                      const EquipmentRegistry &eq_reg);
};
