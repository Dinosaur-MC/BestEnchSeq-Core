#include "Enchantment.h"

bool EnchInfo::operator==(const EnchInfo& other) const {
    return name_id == other.name_id;
}

#include <algorithm>

EnchSet::iterator EnchSet::find_by_id(int32_t id) {
    return std::find_if(begin(), end(), [id](const Ench& e) { return e.id == id; });
}
EnchSet::const_iterator EnchSet::find_by_id(int32_t id) const {
    return std::find_if(begin(), end(), [id](const Ench& e) { return e.id == id; });
}
