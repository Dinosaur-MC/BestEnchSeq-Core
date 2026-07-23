#pragma once
#include "domain/business/types/Equipment.h"

#include <string>
#include <unordered_map>
#include <vector>

// ─── Equipment registry ───
//
// Manages Equipment instances. Numeric ID = index in instances_.
// initialize() sets up the data; after that the registry is read-only.
// Supports bidirectional lookup: NSID id ↔ int32_t index.
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

    // NSID lookup (O(1) average). Throws std::out_of_range if not found.
    const Equipment& get(const NSID& id) const;

    // NSID → numeric index. Returns -1 if not found.
    int32_t get_id(const NSID& id) const;

    // Query by category
    std::vector<const Equipment*> get_by_category(const NSID& category) const;

    // NSID -> pointer map
    std::unordered_map<NSID, const Equipment*> get_name_map() const;

    /// Add a single Equipment. Returns false if id already exists.
    bool add(const Equipment& eq);

    /// Remove by id. Returns false if not found.
    bool remove(const NSID& id);

    // All instances (for iteration)
    const std::vector<Equipment>& get_instances() const { return instances_; }
    size_t size() const { return instances_.size(); }

private:
    std::vector<Equipment> instances_;
    std::unordered_map<NSID, int32_t> name_to_id_;
};
