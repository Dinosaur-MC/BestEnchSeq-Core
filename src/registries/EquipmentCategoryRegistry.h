#pragma once
#include "types/EquipmentCategory.h"

#include <string>
#include <unordered_map>
#include <vector>

// ─── Equipment category registry ───
//
// Singleton registry managing EquipmentCategory definitions.
// Builtin categories are hardcoded with stable numeric IDs.
// Custom categories can be added at runtime via add_custom() or register_or_get_id().
//
// Initialization order:
//   1. initialize() — loads builtins
//   2. (optional) add_custom() — extends with custom categories from data files
//   3. EnchInfoParser / EquipmentParser resolve strings to IDs via this registry
class EquipmentCategoryRegistry {
public:
    static EquipmentCategoryRegistry& get_instance();

    // Lifecycle
    void initialize();
    void add_custom(const std::vector<EquipmentCategory>& categories);

    // Lookup (O(1))
    const EquipmentCategory* get(int32_t id) const;
    const EquipmentCategory* get(const std::string& name_id) const;
    int32_t get_id(const std::string& name_id) const;  // -1 if not found

    // Like get_id(), but registers the category as custom if not found
    int32_t register_or_get_id(const std::string& name_id);

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
