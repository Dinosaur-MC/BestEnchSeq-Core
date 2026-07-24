#pragma once
#include "IRegistry.h"
#include "domain/business/types/EquipmentTag.h"

#include <vector>

// ─── Equipment tag registry ───
//
// Manages EquipmentTag definitions by tag name.
// Each entry maps a short name ("sword", "helmet") to an EquipmentTag
// whose NSID is "#minecraft:<name>".
//
// Convenience: get(name) constructs the NSID "#minecraft:<name>" and
// performs O(1) lookup in the backing unordered_map.
class EquipmentTagRegistry : public IRegistry<EquipmentTag> {
public:
    EquipmentTagRegistry() = default;
    EquipmentTagRegistry(const std::vector<EquipmentTag>& tags);
    EquipmentTagRegistry(const EquipmentTagRegistry&) = default;
    EquipmentTagRegistry& operator=(const EquipmentTagRegistry&) = default;
    EquipmentTagRegistry(EquipmentTagRegistry&&) = default;
    EquipmentTagRegistry& operator=(EquipmentTagRegistry&&) = default;

    /// Convenience: get a tag by its short name.
    /// Equivalent to at(NSID("#minecraft:" + name)).
    /// Throws std::out_of_range if not found.
    const EquipmentTag& get(const std::string& name) const;
};
