#include "TagRegistry.h"
#include <stdexcept>

TagRegistry::TagRegistry(const std::vector<EquipmentTag>& tags) {
    _data.reserve(tags.size());
    for (const auto& tag : tags)
        _data.emplace(tag.id, tag);
}

const EquipmentTag& TagRegistry::get(const std::string& name) const {
    auto it = _data.find(NSID("#minecraft:" + name));
    if (it == _data.end()) {
        auto msg = std::string("EquipmentTag not found: ") + name;
        throw std::out_of_range(msg.c_str());
    }
    return it->second;
}
