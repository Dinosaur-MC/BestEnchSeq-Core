#pragma once
#include "Enchantment.h"
#include "Equipment.h"

#include <optional>
#include <vector>

/// Forgeable item stack — pure data container.
struct Item {
    std::optional<Equipment> equipment;
    EnchSet enchantments;
    int32_t prior_penalty;
    int32_t durability;
    int32_t priority = 99;  // lower = more preferred (inventory mode)

    Item();
    Item(const EnchSet &enchs, int32_t prior_penalty = 0);
    /// Construct an equipment item (copies the Equipment descriptor).
    Item(const Equipment &equip, const EnchSet &enchs, int32_t prior_penalty, int32_t durability);
    Item(const Equipment &equip, const EnchSet &enchs, int32_t prior_penalty = 0);

    bool is_book() const;
    bool is_equipment() const;
};

using ItemCollection = std::vector<Item>;
