#pragma once
#include "types/EquipmentCategory.h"

#include <string>
#include <unordered_map>
#include <vector>

// ─── Equipment category registry ───
//
// Singleton registry managing EquipmentCategory definitions.
// Builtin categories are hardcoded with stable numeric IDs.
// Optional custom categories can be passed to initialize().
// After initialize(), the registry is immutable — no add_custom/register.
//
// Matches EnchantmentRegistry pattern: get() returns reference, throws on invalid.
class EquipmentCategoryRegistry {
public:
    static EquipmentCategoryRegistry& get_instance();

    EquipmentCategoryRegistry() = default;
    EquipmentCategoryRegistry(const EquipmentCategoryRegistry&) = delete;
    EquipmentCategoryRegistry& operator=(const EquipmentCategoryRegistry&) = delete;

    // Lifecycle — sets up builtins + optional custom categories.
    // After this call the registry is immutable.
    void initialize(const std::vector<EquipmentCategory>& custom_categories = {});

    // Lookup (O(1)) — throws std::out_of_range on invalid input
    const EquipmentCategory& get(int32_t id) const;
    const EquipmentCategory& get(const std::string& name_id) const;
    int32_t get_id(const std::string& name_id) const;  // -1 if not found

    const std::vector<EquipmentCategory>& get_instances() const { return instances_; }
    size_t size() const { return instances_.size(); }

    // ── Builtin category IDs (stable across versions) ──
    static constexpr int32_t ID_ANY         = 0;
    static constexpr int32_t ID_SWORD       = 1;
    static constexpr int32_t ID_HELMET      = 2;
    static constexpr int32_t ID_CHESTPLATE  = 3;
    static constexpr int32_t ID_LEGGINGS    = 4;
    static constexpr int32_t ID_BOOTS       = 5;
    static constexpr int32_t ID_PICKAXE     = 6;
    static constexpr int32_t ID_AXE         = 7;
    static constexpr int32_t ID_SHOVEL      = 8;
    static constexpr int32_t ID_HOE         = 9;
    static constexpr int32_t ID_BOW         = 10;
    static constexpr int32_t ID_SHIELD      = 11;
    static constexpr int32_t ID_CROSSBOW    = 12;
    static constexpr int32_t ID_TRIDENT     = 13;
    static constexpr int32_t ID_FISHING_ROD = 14;

private:
    void add_builtin(int32_t id, std::string name_id);

    std::vector<EquipmentCategory> instances_;
    std::unordered_map<std::string, int32_t> name_to_id_;
};
