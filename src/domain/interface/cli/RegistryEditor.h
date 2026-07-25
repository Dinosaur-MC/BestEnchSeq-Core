#pragma once
#include "domain/business/registries/EquipmentTagRegistry.h"
#include <string>

class EnchantmentRegistry;
class EquipmentRegistry;
class Profile;

/// Parse and apply --registry-edit operations to domain registries.
/// Format per operation: <target>:<action>,<id>[,<field>=<value>...]
/// Operations are separated by semicolon.
/// Throws std::runtime_error on invalid operations.
void apply_registry_edits(
    const std::string& ops,
    EnchantmentRegistry& ench_reg,
    EquipmentRegistry& eq_reg,
    EquipmentTagRegistry& cat_reg);

/// Parse and apply --registry-edit operations to a Profile.
/// Same format as the raw-registry overload, but operates through
/// Profile proxy methods (add_enchantment, remove_equipment, etc.).
void apply_registry_edits(
    const std::string& ops,
    Profile& profile);
