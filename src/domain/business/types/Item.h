#pragma once
#include "EnchSet.h"
#include "common/CommonTypes.h"
#include "common/serialization/IJsonSerializable.h"
#include "common/utils/HashUtils.hpp"
#include <vector>

/// Forgeable item stack — pure data container.
struct Item : IJsonSerializable {
    NSID id;
    EnchSet enchantments;
    int32_t prior_penalty;
    int32_t durability;

    Item() : prior_penalty(0), durability(0) {}
    Item(NSID id_, const EnchSet &enchs_, int32_t ppn_, int32_t dur_);
    Item(NSID id_, const EnchSet &enchs_, int32_t ppn_ = 0);

    bool operator==(const Item &o) const { return id == o.id; }
    auto operator<=>(const Item &o) const { return id <=> o.id; }

    bool is_book() const {
        static const NSID book("minecraft:book");
        static const NSID enchanted_book("minecraft:enchanted_book");
        return id == book || id == enchanted_book;
    }
    bool is_equipment() const { return !is_book() && durability > 0; }

    // -- ISerializable --
    Json to_json() const override;
    void from_json(const Json& json) override;
};

template <> struct std::hash<Item> {
    size_t operator()(const Item &item) const noexcept {
        size_t h = std::hash<NSID>()(item.id);
        hash_combine(h, std::hash<EnchSet>()(item.enchantments));
        hash_combine(h, item.prior_penalty);
        hash_combine(h, item.durability);
        return h;
    }
};

using ItemCollection = std::vector<Item>;
