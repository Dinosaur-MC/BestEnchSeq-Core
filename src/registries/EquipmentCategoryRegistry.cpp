#include "registries/EquipmentCategoryRegistry.h"
#include <stdexcept>

EquipmentCategoryRegistry* EquipmentCategoryRegistry::s_override_ = nullptr;

EquipmentCategoryRegistry& EquipmentCategoryRegistry::get_instance() {
    static EquipmentCategoryRegistry s_default;
    return s_override_ ? *s_override_ : s_default;
}

void EquipmentCategoryRegistry::set_instance(EquipmentCategoryRegistry* reg) {
    s_override_ = reg;
}

void EquipmentCategoryRegistry::reset() {
    instances_.clear();
    name_to_id_.clear();

    auto add = [&](int32_t id, std::string name_id) {
        instances_.push_back({id, name_id});
        name_to_id_[std::move(name_id)] = id;
    };

    add(ID_ANY,         "any");
    add(ID_SWORD,       "sword");
    add(ID_HELMET,      "helmet");
    add(ID_CHESTPLATE,  "chestplate");
    add(ID_LEGGINGS,    "leggings");
    add(ID_BOOTS,       "boots");
    add(ID_PICKAXE,     "pickaxe");
    add(ID_AXE,         "axe");
    add(ID_SHOVEL,      "shovel");
    add(ID_HOE,         "hoe");
    add(ID_BOW,         "bow");
    add(ID_SHIELD,      "shield");
    add(ID_CROSSBOW,    "crossbow");
    add(ID_TRIDENT,     "trident");
    add(ID_FISHING_ROD, "fishing_rod");
}

void EquipmentCategoryRegistry::initialize(const std::vector<std::string>& custom_category_names) {
    reset();

    // Append custom category names (duplicates of builtins are silently skipped)
    for (const auto& cat_name : custom_category_names) {
        if (name_to_id_.count(cat_name))
            continue;  // skip duplicates
        int32_t id = static_cast<int32_t>(instances_.size());
        instances_.push_back({id, cat_name});
        name_to_id_[cat_name] = id;
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
