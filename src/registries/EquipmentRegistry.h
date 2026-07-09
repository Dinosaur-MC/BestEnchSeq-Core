#pragma once
#include "types/Equipment.h"

#include <string>
#include <unordered_map>
#include <vector>

// ─── Equipment registry ───
//
// Singleton registry managing Equipment instances, matching EnchantmentRegistry's
// pattern. Numeric ID = index in instances_. initialize() is called once at
// startup, after which instances_ is never modified — pointers are stable.
// Supports bidirectional lookup: int32_t id ↔ string name_id.
class EquipmentRegistry {
public:
    static EquipmentRegistry& get_instance();

    EquipmentRegistry() = default;
    EquipmentRegistry(const EquipmentRegistry&) = delete;
    EquipmentRegistry& operator=(const EquipmentRegistry&) = delete;

    void initialize(const std::vector<Equipment>& eq_list);

    // Numeric ID lookup (O(1)). Returns nullptr if out of range.
    const Equipment* get(int32_t id) const;

    // String lookup (O(1) average). Returns nullptr if not found.
    const Equipment* get(const std::string& name_id) const;

    // String → numeric ID. Returns -1 if not found.
    int32_t get_id(const std::string& name_id) const;

    // All instances (for iteration)
    const std::vector<Equipment>& get_instances() const { return instances_; }

private:
    std::vector<Equipment> instances_;
    std::unordered_map<std::string, int32_t> name_to_id_;
};
