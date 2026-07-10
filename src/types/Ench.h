#pragma once

#include <cstddef>
#include <cstdint>

struct Ench {
    int32_t id = 0;
    int32_t level = 1;

    Ench() = default;
    Ench(int32_t id, int32_t level) : id(id), level(level) {}

    struct Hash {
        size_t operator()(const Ench& e) const {
            return static_cast<size_t>(e.id) ^ (static_cast<size_t>(e.level) << 16);
        }
    };

    bool operator==(const Ench& other) const { return id == other.id && level == other.level; }
};
