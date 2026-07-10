#pragma once
#include "types/EquipmentCategory.h"

#include <string>
#include <unordered_map>
#include <vector>

// ─── Equipment category registry ───
//
// Manages EquipmentCategory definitions. Builtin categories have stable
// numeric IDs. Optional custom category names can be passed to initialize().
// After initialize(), the registry is immutable — no add_custom/register.
//
// Matches EnchantmentRegistry pattern: get() returns reference, throws on invalid.
class EquipmentCategoryRegistry {
public:
    EquipmentCategoryRegistry() = default;
    EquipmentCategoryRegistry(const EquipmentCategoryRegistry&) = default;
    EquipmentCategoryRegistry& operator=(const EquipmentCategoryRegistry&) = default;

    // Lifecycle — resets builtins and optionally appends custom category names.
    // After this call the registry is immutable.
    void initialize(const std::vector<std::string>& custom_category_names = {});

    // Lookup (O(1)) — throws std::out_of_range on invalid input
    const EquipmentCategory& get(int32_t id) const;
    const EquipmentCategory& get(const std::string& name_id) const;
    int32_t get_id(const std::string& name_id) const;  // -1 if not found

    const std::vector<EquipmentCategory>& get_instances() const { return instances_; }
    size_t size() const { return instances_.size(); }

    // Builtin category IDs moved to EquipmentCategory struct.
    // Use EquipmentCategory::ID_SWORD, EquipmentCategory::NAME_SWORD, etc.

private:
    /// Reset to builtin defaults (clears any custom categories).
    void reset();

    std::vector<EquipmentCategory> instances_;
    std::unordered_map<std::string, int32_t> name_to_id_;
};
