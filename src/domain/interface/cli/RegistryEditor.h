#pragma once
#include "domain/business/registries/EquipmentTagRegistry.h"
#include <string>

class EnchantmentRegistry;
class EquipmentRegistry;

/// Parse and apply --registry-edit operations to domain registries.
/// Format per operation: <target>:<action>,<id>[,<field>=<value>...]
/// Operations are separated by semicolon.
/// Throws std::runtime_error on invalid operations.
void apply_registry_edits(
    const std::string& ops,
    EnchantmentRegistry& ench_reg,
    EquipmentRegistry& eq_reg,
    EquipmentTagRegistry& cat_reg);
