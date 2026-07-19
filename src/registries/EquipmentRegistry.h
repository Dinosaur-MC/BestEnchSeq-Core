#pragma once
#include "types/Equipment.h"

#include <string>
#include <unordered_map>
#include <vector>

// ─── Equipment registry ───
//
// Manages Equipment instances. Numeric ID = index in instances_.
// initialize() sets up the data; after that the registry is read-only.
// Supports bidirectional lookup: int32_t id ↔ string name_id.
class EquipmentRegistry {
public:
    EquipmentRegistry() = default;
    EquipmentRegistry(const EquipmentRegistry&) = default;
    EquipmentRegistry& operator=(const EquipmentRegistry&) = default;

    void initialize(const std::vector<Equipment>& eq_list);
    /// Reset all state — for testing only. Invalidates all held references.
    void reset_for_testing();

    // Numeric ID lookup (O(1)). Throws std::out_of_range on invalid.
    const Equipment& get(int32_t id) const;

    // String lookup (O(1) average). Throws std::out_of_range if not found.
    const Equipment& get(const std::string& name_id) const;

    // String → numeric ID. Returns -1 if not found.
    int32_t get_id(const std::string& name_id) const;

    // Query by category
    std::vector<const Equipment*> get_by_category(int32_t category_id) const;

    // Name -> pointer map
    std::unordered_map<std::string, const Equipment*> get_name_map() const;

    /// Add a single Equipment. Returns false if name_id already exists.
    bool add(const Equipment& eq);

    /// Remove by name_id. Returns false if not found.
    bool remove(const std::string& name_id);

    // All instances (for iteration)
    const std::vector<Equipment>& get_instances() const { return instances_; }
    size_t size() const { return instances_.size(); }

private:
    std::vector<Equipment> instances_;
    std::unordered_map<std::string, int32_t> name_to_id_;
};
