#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/CommonTypes.h"

struct Ench {
    int32_t id    = 0;
    int32_t level = 1;

    Ench() = default;
    Ench(int32_t id, int32_t level) : id(id), level(level) {}

    struct Hash {
        size_t operator()(const Ench &e) const {
            return static_cast<size_t>(e.id) ^ (static_cast<size_t>(e.level) << 16);
        }
    };

    bool operator==(const Ench &other) const { return id == other.id && level == other.level; }
};

struct EnchInfo {
    std::string name_id;
    std::string name;
    MCE supported_platform = MCE::None;
    int32_t max_level      = 0;
    int32_t limited_level  = 0;
    int32_t multiplier     = 0;
    bool is_treasure       = false;
    std::unordered_set<std::string> exclusive_set;
    std::unordered_set<int32_t> applicable_category_ids;

    bool operator==(const EnchInfo &other) const;

    struct Hash {
        size_t operator()(const EnchInfo &info) const { return std::hash<std::string>()(info.name_id); }
    };
};

using EnchInfoList = std::vector<EnchInfo>;

/// Enchantment set — pure data container.
/// `operator==` compares all members for strict equality.
/// Use `find_by_id()` for id-only lookups.
class EnchSet : public std::unordered_set<Ench, Ench::Hash> {
  public:
    using std::unordered_set<Ench, Ench::Hash>::unordered_set;

    iterator find_by_id(int32_t id);
    const_iterator find_by_id(int32_t id) const;
};
