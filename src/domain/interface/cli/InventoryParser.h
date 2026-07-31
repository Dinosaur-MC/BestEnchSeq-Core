#pragma once
#include "domain/business/types/Item.h"
#include <string>
#include <vector>

class EnchantmentRegistry;
class EquipmentRegistry;

/// Parsed inventory file: available items and their forge priorities.
struct InventoryInput {
    ItemCollection items;
    std::vector<int32_t> priorities;  // parallel to items; absent → 99
};

struct InventoryParser {
    /// Parse an inventory JSON file into available items + priorities.
    ///
    /// File format (see SRS §3):
    ///   { "items": [ { "type": "book"|"equipment", "id": "...",
    ///                  "enchants": [ { "id": "...", "level": N }, ... ],
    ///                  "prior_penalty": N, "durability": N, "priority": N },
    ///                ... ] }
    /// Throws std::runtime_error on file read / parse errors, unknown
    /// enchantments, unknown equipment, or malformed item entries.
    static InventoryInput parse_file(const std::string &path,
                                     const EnchantmentRegistry &ench_reg,
                                     const EquipmentRegistry &eq_reg);
};
