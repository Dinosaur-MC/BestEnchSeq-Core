#pragma once
#include "IRegistry.h"
#include "domain/business/types/EquipmentTag.h"

#include <vector>

// ─── Tag registry ───
//
// Manages tag definitions by tag name.
// Each entry maps a short name ("sword", "helmet") to a real Minecraft tag
// whose NSID is "#minecraft:<name>".
//
// Convenience: get(name) constructs the NSID "#minecraft:<name>" and
// performs O(1) lookup in the backing unordered_map.
class TagRegistry : public IRegistry<EquipmentTag> {
public:
    TagRegistry() = default;
    TagRegistry(const std::vector<EquipmentTag>& tags);
    TagRegistry(const TagRegistry&) = default;
    TagRegistry& operator=(const TagRegistry&) = default;
    TagRegistry(TagRegistry&&) = default;
    TagRegistry& operator=(TagRegistry&&) = default;

    /// Convenience: get a tag by its short name.
    /// Equivalent to at(NSID("#minecraft:" + name)).
    /// Throws std::out_of_range if not found.
    const EquipmentTag& get(const std::string& name) const;
};
