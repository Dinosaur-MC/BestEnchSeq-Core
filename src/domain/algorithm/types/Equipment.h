#pragma once
#include "common/serialization/IBinarySerializable.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace algorithm {

struct Equipment : IBinarySerializable {
    int32_t id;
    int32_t category_id;
    int32_t max_durability;

    /// Local IDs of enchantments applicable to this equipment.
    /// Populated during EnchReg init; all enchantments in the compact
    /// registry are guaranteed to be applicable to the target equipment.
    std::vector<int16_t> applicable_enchs;

    struct Hash {
        size_t operator()(const Equipment &eq) const { return std::hash<size_t>()(eq.id); }
    };

    bool operator==(const Equipment &other) const;

    void serialize(ByteStreamWriter &w) const noexcept override {
        w << id << category_id << max_durability << applicable_enchs;
    }
    void deserialize(ByteStreamReader &r) noexcept override {
        r >> id >> category_id >> max_durability >> applicable_enchs;
    }
};

} // namespace algorithm
