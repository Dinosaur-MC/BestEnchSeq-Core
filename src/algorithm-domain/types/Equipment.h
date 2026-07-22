#pragma once
#include <cstdint>
#include <string>
#include <unordered_set>

namespace algorithm {

struct Equipment {
    int32_t id;
    int32_t category_id;
    int32_t max_durability;

    struct Hash {
        size_t operator()(const Equipment &eq) const { return std::hash<size_t>()(eq.id); }
    };

    bool operator==(const Equipment &other) const;
};

} // namespace algorithm
