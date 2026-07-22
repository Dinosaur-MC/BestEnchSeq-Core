#pragma once
#include "types/ItemStack.h"
#include <filesystem>
#include <string>
#include <vector>

class EnchantmentRegistry;
class EquipmentRegistry;

/// Result of inventory parsing -- resolved items ready for use.
struct InventoryInput {
    ItemCollection items;               // sorted by priority
    std::vector<std::string> warnings;  // unknown enchantments/equipment
};

/// Parse an inventory JSON file and resolve all string IDs
/// to registry int32_t IDs.
///
/// Handles both book and equipment items, validates existence
/// against registries, and sorts by priority.
struct InventoryResolver {
    static InventoryInput resolve(
        const std::filesystem::path& path,
        const EnchantmentRegistry& ench_reg,
        const EquipmentRegistry& eq_reg
    );
};
