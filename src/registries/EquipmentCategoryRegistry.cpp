#include "registries/EquipmentCategoryRegistry.h"

EquipmentCategoryRegistry& EquipmentCategoryRegistry::get_instance() {
    static EquipmentCategoryRegistry instance;
    return instance;
}

void EquipmentCategoryRegistry::add_builtin(int32_t id, std::string name_id) {
    instances_.push_back({id, std::move(name_id)});
}

void EquipmentCategoryRegistry::initialize() {
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

    for (const auto& cat : instances_)
        name_to_id_[cat.name_id] = cat.id;
}

void EquipmentCategoryRegistry::add_custom(const std::vector<EquipmentCategory>& categories) {
    for (const auto& cat : categories) {
        if (name_to_id_.count(cat.name_id))
            continue;  // already exists
        instances_.push_back(cat);
        name_to_id_[cat.name_id] = cat.id;
    }
}

const EquipmentCategory* EquipmentCategoryRegistry::get(int32_t id) const {
    if (id >= 0 && id < static_cast<int32_t>(instances_.size()))
        return &instances_[static_cast<size_t>(id)];
    return nullptr;
}

const EquipmentCategory* EquipmentCategoryRegistry::get(const std::string& name_id) const {
    auto it = name_to_id_.find(name_id);
    if (it != name_to_id_.end())
        return &instances_[static_cast<size_t>(it->second)];
    return nullptr;
}

int32_t EquipmentCategoryRegistry::get_id(const std::string& name_id) const {
    auto it = name_to_id_.find(name_id);
    return it != name_to_id_.end() ? it->second : -1;
}

int32_t EquipmentCategoryRegistry::register_or_get_id(const std::string& name_id) {
    auto it = name_to_id_.find(name_id);
    if (it != name_to_id_.end())
        return it->second;
    int32_t new_id = static_cast<int32_t>(instances_.size());
    instances_.push_back({new_id, name_id});
    name_to_id_[name_id] = new_id;
    return new_id;
}
