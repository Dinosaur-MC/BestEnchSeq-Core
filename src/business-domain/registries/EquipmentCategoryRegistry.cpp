#include "EquipmentCategoryRegistry.h"
#include <stdexcept>

void EquipmentCategoryRegistry::reset() {
    instances_.clear();
    name_to_id_.clear();

    auto add = [&](int32_t id, std::string name_id) {
        instances_.push_back({id, name_id});
        name_to_id_[std::move(name_id)] = id;
    };

    add(EquipmentCategory::ID_ANY,         std::string(EquipmentCategory::NAME_ANY));
    add(EquipmentCategory::ID_SWORD,       std::string(EquipmentCategory::NAME_SWORD));
    add(EquipmentCategory::ID_HELMET,      std::string(EquipmentCategory::NAME_HELMET));
    add(EquipmentCategory::ID_CHESTPLATE,  std::string(EquipmentCategory::NAME_CHESTPLATE));
    add(EquipmentCategory::ID_LEGGINGS,    std::string(EquipmentCategory::NAME_LEGGINGS));
    add(EquipmentCategory::ID_BOOTS,       std::string(EquipmentCategory::NAME_BOOTS));
    add(EquipmentCategory::ID_PICKAXE,     std::string(EquipmentCategory::NAME_PICKAXE));
    add(EquipmentCategory::ID_AXE,         std::string(EquipmentCategory::NAME_AXE));
    add(EquipmentCategory::ID_SHOVEL,      std::string(EquipmentCategory::NAME_SHOVEL));
    add(EquipmentCategory::ID_HOE,         std::string(EquipmentCategory::NAME_HOE));
    add(EquipmentCategory::ID_BOW,         std::string(EquipmentCategory::NAME_BOW));
    add(EquipmentCategory::ID_SHIELD,      std::string(EquipmentCategory::NAME_SHIELD));
    add(EquipmentCategory::ID_CROSSBOW,    std::string(EquipmentCategory::NAME_CROSSBOW));
    add(EquipmentCategory::ID_TRIDENT,     std::string(EquipmentCategory::NAME_TRIDENT));
    add(EquipmentCategory::ID_FISHING_ROD, std::string(EquipmentCategory::NAME_FISHING_ROD));
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

int32_t EquipmentCategoryRegistry::add(const std::string& name) {
    int32_t existing = get_id(name);
    if (existing >= 0) return existing;
    int32_t id = static_cast<int32_t>(instances_.size());
    instances_.push_back({id, name});
    name_to_id_[name] = id;
    return id;
}
