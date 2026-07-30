#pragma once
#include "common/serialization/IBinarySerializable.h"
#include "utils/HashUtils.hpp"
#include <cstdint>
#include <cstring>
#include <vector>

namespace algorithm {

using mask_type = uint64_t;

struct EnchInfo : IBinarySerializable {
    uint8_t id         = 0;
    uint8_t mul        = 0;
    uint8_t mul_b      = 0;
    uint8_t max_lvl    = 0;
    mask_type exc_mask = 0;
    bool applicable    = false;

    [[nodiscard]] bool is_conflict(const EnchInfo &other) const noexcept;

    void serialize(ByteStreamWriter &w) const noexcept override {
        w << id << mul << mul_b << max_lvl << exc_mask << static_cast<uint8_t>(applicable);
    }
    void deserialize(ByteStreamReader &r) noexcept override {
        uint8_t app;
        r >> id >> mul >> mul_b >> max_lvl >> exc_mask >> app;
        applicable = app != 0;
    }
};

struct Ench {
    using value_type = uint8_t;

    value_type id{0};
    value_type level{0};

    Ench() = default;
    Ench(value_type id_, value_type level_) noexcept : id(id_), level(level_) {}

    bool operator==(const Ench &o) const noexcept { return id == o.id && level == o.level; }
    bool valid() const noexcept { return level > 0; }

    void serialize(ByteStreamWriter &w) const noexcept { w << id << level; }
    void deserialize(ByteStreamReader &r) noexcept { r >> id >> level; }
};
static_assert(std::has_unique_object_representations_v<Ench>);
using EnchCollection = std::vector<Ench>;

inline ByteStreamWriter &operator<<(ByteStreamWriter &w, const Ench &e) {
    e.serialize(w);
    return w;
}
inline ByteStreamReader &operator>>(ByteStreamReader &r, Ench &e) {
    e.deserialize(r);
    return r;
}

} // namespace algorithm

template <> struct std::hash<algorithm::Ench> {
    size_t operator()(const algorithm::Ench &e) const noexcept {
        return static_cast<size_t>(e.id) ^ (static_cast<size_t>(e.level) << 16);
    }
};

template <> struct std::hash<algorithm::EnchCollection> {
    size_t operator()(const algorithm::EnchCollection &enchs) const noexcept {
        size_t h = enchs.size();
        for (const auto &ench : enchs)
            hash_combine(h, std::hash<algorithm::Ench>()(ench));
        return h;
    }
};
