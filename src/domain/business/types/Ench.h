#pragma once
#include "common/CommonTypes.h"
#include <string>

struct Ench {
    NSID id;
    std::string name;
    int32_t level = 1;

    Ench() = default;
    Ench(NSID id_, std::string name_, int32_t level_ = 1)
        : id(std::move(id_)), name(std::move(name_)), level(level_) {}

    bool operator==(const Ench &o) const noexcept { return id == o.id && level == o.level; }
    bool operator<(const Ench &o) const noexcept {
        if (id == o.id) return level < o.level;
        return id.str() < o.id.str();
    }
};

template <> struct std::hash<Ench> {
    size_t operator()(const Ench &e) const noexcept {
        size_t h = std::hash<NSID>()(e.id);
        h ^= static_cast<size_t>(e.level) << 16;
        return h;
    }
};
