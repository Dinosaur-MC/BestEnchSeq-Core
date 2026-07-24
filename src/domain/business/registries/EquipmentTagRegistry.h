#pragma once
#include "IRegistry.h"
#include "domain/business/types/EquipmentTag.h"

#include <string>
#include <unordered_map>
#include <vector>

// ─── Equipment tag registry ───
//
// Manages EquipmentTag definitions by tag name.
// Each entry maps a short name ("sword", "helmet") to an EquipmentTag
// whose NSID is "#minecraft:<name>".
//
// Builtin tags are initialized via initialize() with the custom tag names
// collected from equipment data. The name_to_id_ map enables O(1) lookup
// by the short tag name string.
class EquipmentTagRegistry : public IRegistry<EquipmentTag> {
public:
    EquipmentTagRegistry() = default;
    EquipmentTagRegistry(const std::vector<EquipmentTag>& tags);

    // ── Tag-name lookup ─────────────────────────────────────────────────

    /// Convenience: get a tag by its short name.
    /// Throws std::out_of_range if not found.
    const EquipmentTag& get(const std::string& name) const;

    // ── IRegistry overrides ─────────────────────────────────────────────
    bool insert(const EquipmentTag& item) override;
    bool remove(const NSID& id) override;
    void clear() noexcept override;

private:
    std::unordered_map<std::string, int32_t> name_to_id_;
};
