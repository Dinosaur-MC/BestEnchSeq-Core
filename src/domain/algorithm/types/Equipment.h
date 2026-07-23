#pragma once
#include "common/io/ISerializable.h"
#include <cstdint>
#include <functional>

namespace algorithm {

struct Equipment : ISerializable {
    int32_t id;
    int32_t category_id;
    int32_t max_durability;

    struct Hash {
        size_t operator()(const Equipment &eq) const { return std::hash<size_t>()(eq.id); }
    };

    bool operator==(const Equipment &other) const;

    void serialize(ByteStreamWriter &w) const noexcept override {
        w << id << category_id << max_durability;
    }
    void deserialize(ByteStreamReader &r) noexcept override {
        r >> id >> category_id >> max_durability;
    }
};

} // namespace algorithm
