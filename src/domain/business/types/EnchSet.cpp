#include "EnchSet.h"
#include <algorithm>

EnchSet::iterator EnchSet::find(const NSID &ench_id) {
    return std::find_if(begin(), end(), [&](const Ench &e) { return e.id == ench_id; });
}
EnchSet::const_iterator EnchSet::find(const NSID &ench_id) const {
    return std::find_if(begin(), end(), [&](const Ench &e) { return e.id == ench_id; });
}

Json EnchSet::to_json() const {
    Json arr = Json::array();
    for (const auto& ench : *this)
        arr.push_back(ench.to_json());
    return arr;
}

void EnchSet::from_json(const Json& json) {
    clear();
    auto arr = json.as_array();
    for (const auto& elem : arr) {
        Ench e;
        e.from_json(elem);
        insert(std::move(e));
    }
}
