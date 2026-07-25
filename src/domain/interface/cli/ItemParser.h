#pragma once
#include "domain/business/types/Item.h"
#include <string>

class EnchantmentRegistry;
class EquipmentRegistry;

struct ItemParser {
    /// Parse target spec: <item_id>[<ench>=<level>,...]
    /// Bare item_id also accepted (no inline enchants).
    /// Resolves equipment and enchantments against the given registries.
    /// Throws std::runtime_error on unknown equipment, unknown enchantments,
    /// unmatched brackets, or trailing content after ']'.
    static Item parse(const std::string &input,
                      const EnchantmentRegistry &ench_reg,
                      const EquipmentRegistry &eq_reg);
};
