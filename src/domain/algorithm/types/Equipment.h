#pragma once
#include "CommonTypes.h"
#include "common/serialization/IBinarySerializable.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <unordered_set>

namespace algorithm {

struct Equipment : IBinarySerializable {
    NSID id;
    int32_t max_durability = 0;
    std::unordered_set<uint8_t> applicable_enchs;

    bool operator==(const Equipment &other) const {
        return id == other.id && max_durability == other.max_durability &&
               applicable_enchs == other.applicable_enchs;
    }

    void serialize(ByteStreamWriter &w) const noexcept override {
        std::vector<uint8_t> enchs_vec(applicable_enchs.begin(), applicable_enchs.end());
        std::sort(enchs_vec.begin(), enchs_vec.end());
        w << id.str() << max_durability << enchs_vec;
    }
    void deserialize(ByteStreamReader &r) noexcept override {
        std::string id_str;
        std::vector<uint8_t> enchs_vec;
        r >> id_str >> max_durability >> enchs_vec;
        id               = NSID(id_str);
        applicable_enchs = std::unordered_set<uint8_t>(enchs_vec.begin(), enchs_vec.end());
    }
};

} // namespace algorithm

template <> struct std::hash<algorithm::Equipment> {
    size_t operator()(const algorithm::Equipment &eq) const noexcept { return std::hash<NSID>()(eq.id); }
};
