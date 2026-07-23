#pragma once
#include "EnchSet.h"
#include "common/CommonTypes.h"
#include <vector>

/// Forgeable item stack — pure data container.
struct Item {
    NSID id;                  // was optional<Equipment> equipment
    EnchSet enchantments;
    int32_t prior_penalty;
    int32_t durability;
    int32_t priority = 99;

    Item() : prior_penalty(0), durability(0) {}
    Item(NSID id_, const EnchSet& enchs_, int32_t ppn_, int32_t dur_);
    Item(NSID id_, const EnchSet& enchs_, int32_t ppn_ = 0);

    bool is_book() const {
        static const NSID book("minecraft:book");
        static const NSID enchanted_book("minecraft:enchanted_book");
        return id == book || id == enchanted_book;
    }
    bool is_equipment() const { return !is_book(); }
};

using ItemCollection = std::vector<Item>;
