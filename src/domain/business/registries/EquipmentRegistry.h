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
//
// Numeric index = position in _data. An O(1) name_to_id_ map supports
// NSID → index lookups.
class EquipmentRegistry : public IRegistry<Equipment> {
public:
    EquipmentRegistry() = default;
    EquipmentRegistry(const std::vector<Equipment>& eq_list);
    EquipmentRegistry(const EquipmentRegistry&) = default;
    EquipmentRegistry& operator=(const EquipmentRegistry&) = default;

    // ── Lookup ─────────────────────────────────────────────────────────

    /// Numeric ID lookup (O(1)). Throws std::out_of_range on invalid.
    const Equipment& get(int32_t id) const;

    /// NSID lookup (O(1) average). Throws std::out_of_range if not found.
    const Equipment& get(const NSID& id) const;

    /// Query by category.
    std::vector<const Equipment*> get_by_category(const NSID& category) const;

    /// Build an NSID → pointer map from current data.
    std::unordered_map<NSID, const Equipment*> get_name_map() const;

    // ── IRegistry overrides ────────────────────────────────────────────
    iterator find(const NSID& id) override;
    const_iterator find(const NSID& id) const override;
    bool insert(const Equipment& item) override;
    bool remove(const NSID& id) override;
    void clear() noexcept override;

private:
    std::unordered_map<NSID, int32_t> name_to_id_;
};
