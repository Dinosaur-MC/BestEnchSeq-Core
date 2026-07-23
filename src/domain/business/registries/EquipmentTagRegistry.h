#pragma once
#include "domain/business/types/EquipmentTag.h"
#include <string>
#include <unordered_map>
#include <vector>

// ─── Equipment tag registry ───
//
// Manages EquipmentTag definitions. Builtin tags are initialized from
// EquipmentTag::sword() etc. accessors. Optional custom tag names can be
// passed to initialize(). After initialize(), the registry is immutable.
//
// Matches EnchantmentRegistry pattern: get() returns reference, throws on invalid.
class EquipmentTagRegistry {
public:
    EquipmentTagRegistry() = default;

    // Lifecycle — resets builtins and optionally appends custom tag names.
    // After this call the registry is immutable.
    void initialize(const std::vector<std::string>& custom_tag_names = {});

    // Lookup (O(1)) — throws std::out_of_range on invalid input
    const EquipmentTag& get(const std::string& name_id) const;
    const EquipmentTag& at(size_t index) const;
    bool contains(const std::string& name_id) const;
    int32_t get_id(const std::string& name_id) const;  // -1 if not found
    size_t size() const { return instances_.size(); }

    const std::vector<EquipmentTag>& get_instances() const { return instances_; }

private:
    /// Reset to builtin defaults.
    void reset();

    std::vector<EquipmentTag> instances_;
    std::unordered_map<std::string, int32_t> name_to_id_;
};
