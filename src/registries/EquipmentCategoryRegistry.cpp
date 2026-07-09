#include "registries/EquipmentCategoryRegistry.h"
#include <stdexcept>

EquipmentCategoryRegistry& EquipmentCategoryRegistry::get_instance() {
    static EquipmentCategoryRegistry instance;
    return instance;
}

void EquipmentCategoryRegistry::add_builtin(int32_t id, std::string name_id) {
    instances_.push_back({id, std::move(name_id)});
}

void EquipmentCategoryRegistry::initialize(const std::vector<EquipmentCategory>& custom_categories) {
    instances_.clear();
    name_to_id_.clear();

    add_builtin(ID_ANY,         "any");
    add_builtin(ID_SWORD,       "sword");
    add_builtin(ID_HELMET,      "helmet");
    add_builtin(ID_CHESTPLATE,  "chestplate");
    add_builtin(ID_LEGGINGS,    "leggings");
    add_builtin(ID_BOOTS,       "boots");
    add_builtin(ID_PICKAXE,     "pickaxe");
    add_builtin(ID_AXE,         "axe");
    add_builtin(ID_SHOVEL,      "shovel");
    add_builtin(ID_HOE,         "hoe");
    add_builtin(ID_BOW,         "bow");
    add_builtin(ID_SHIELD,      "shield");
    add_builtin(ID_CROSSBOW,    "crossbow");
    add_builtin(ID_TRIDENT,     "trident");
    add_builtin(ID_FISHING_ROD, "fishing_rod");

    // Build name_to_id_ so duplicate check below works
    for (const auto& cat : instances_)
        name_to_id_[cat.name_id] = cat.id;

    // Append custom categories (must have sequential IDs matching vector index)
    for (auto cat : custom_categories) {
        if (name_to_id_.count(cat.name_id))
            continue;  // skip duplicates of builtins or earlier customs
        cat.id = static_cast<int32_t>(instances_.size());
        instances_.push_back(std::move(cat));
        name_to_id_[instances_.back().name_id] = instances_.back().id;
    }
}

const EquipmentCategory& EquipmentCategoryRegistry::get(int32_t id) const {
    if (id >= 0 && static_cast<size_t>(id) < instances_.size())
        return instances_[static_cast<size_t>(id)];
    throw std::out_of_range("Unknown EquipmentCategory id: " + std::to_string(id));
}

const EquipmentCategory& EquipmentCategoryRegistry::get(const std::string& name_id) const {
    auto it = name_to_id_.find(name_id);
    if (it != name_to_id_.end())
        return instances_[static_cast<size_t>(it->second)];
    throw std::out_of_range("Unknown EquipmentCategory: " + name_id);
}

int32_t EquipmentCategoryRegistry::get_id(const std::string& name_id) const {
    auto it = name_to_id_.find(name_id);
    return it != name_to_id_.end() ? it->second : -1;
}
