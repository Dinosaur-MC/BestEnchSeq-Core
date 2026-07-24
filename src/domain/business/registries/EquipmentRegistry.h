#pragma once
#include "IRegistry.h"
#include "domain/business/types/Equipment.h"

#include <unordered_map>
#include <vector>

// ─── Equipment registry ───
//
// Manages Equipment instances. Each equipment has a unique NSID id.
// initialize() populates the registry; after that items can be added,
// removed, or updated individually.
class EquipmentRegistry : public IRegistry<Equipment> {
public:
    EquipmentRegistry() = default;
    EquipmentRegistry(const std::vector<Equipment>& eq_list);
    EquipmentRegistry(const EquipmentRegistry&) = default;
    EquipmentRegistry& operator=(const EquipmentRegistry&) = default;

    // ── Lookup ─────────────────────────────────────────────────────────

    /// Query by category.
    std::vector<const Equipment*> get_by_category(const NSID& category) const;

    /// Build an NSID → pointer map from current data.
    std::unordered_map<NSID, const Equipment*> get_name_map() const;
};
