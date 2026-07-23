#include "EquipmentTagRegistry.h"
#include <stdexcept>

void EquipmentTagRegistry::reset() {
    instances_.clear();
    name_to_id_.clear();

    auto add = [&](const NSID& id, const std::string& name) {
        int32_t idx = static_cast<int32_t>(instances_.size());
        instances_.push_back({id, name});
        name_to_id_[name] = idx;
    };

    add(EquipmentTag::sword(),       "sword");
    add(EquipmentTag::helmet(),      "helmet");
    add(EquipmentTag::chestplate(),  "chestplate");
    add(EquipmentTag::leggings(),    "leggings");
    add(EquipmentTag::boots(),       "boots");
    add(EquipmentTag::pickaxe(),     "pickaxe");
    add(EquipmentTag::axe(),         "axe");
    add(EquipmentTag::shovel(),      "shovel");
    add(EquipmentTag::hoe(),         "hoe");
    add(EquipmentTag::bow(),         "bow");
    add(EquipmentTag::shield(),      "shield");
    add(EquipmentTag::crossbow(),    "crossbow");
    add(EquipmentTag::trident(),     "trident");
    add(EquipmentTag::fishing_rod(), "fishing_rod");
}

void EquipmentTagRegistry::initialize(const std::vector<std::string>& custom_tag_names) {
    reset();

    // Append custom tag names (duplicates of builtins are silently skipped)
    for (const auto& tag_name : custom_tag_names) {
        if (name_to_id_.count(tag_name))
            continue;  // skip duplicates
        int32_t idx = static_cast<int32_t>(instances_.size());
        instances_.push_back({NSID("#minecraft:" + tag_name), tag_name});
        name_to_id_[tag_name] = idx;
    }
}

const EquipmentTag& EquipmentTagRegistry::get(const std::string& name_id) const {
    auto it = name_to_id_.find(name_id);
    if (it != name_to_id_.end())
        return instances_[static_cast<size_t>(it->second)];
    throw std::out_of_range("Unknown EquipmentTag: " + name_id);
}

const EquipmentTag& EquipmentTagRegistry::at(size_t index) const {
    if (index < instances_.size())
        return instances_[index];
    throw std::out_of_range("EquipmentTag index out of range: " + std::to_string(index));
}

bool EquipmentTagRegistry::contains(const std::string& name_id) const {
    return name_to_id_.find(name_id) != name_to_id_.end();
}

int32_t EquipmentTagRegistry::get_id(const std::string& name_id) const {
    auto it = name_to_id_.find(name_id);
    return it != name_to_id_.end() ? it->second : -1;
}
