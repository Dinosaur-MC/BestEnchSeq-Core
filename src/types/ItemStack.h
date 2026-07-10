#pragma once
#include "EnchSet.h"
#include "Equipment.h"

#include <optional>
#include <vector>

/// Forgeable item stack — pure data container.
struct ItemStack {
    std::optional<Equipment> equipment;
    EnchSet enchantments;
    int32_t prior_penalty;
    int32_t durability;
    int32_t priority = 99;  // lower = more preferred (inventory mode)

    ItemStack();
    ItemStack(const EnchSet &enchs, int32_t prior_penalty = 0);
    /// Construct an equipment item (copies the Equipment descriptor).
    ItemStack(const Equipment &equip, const EnchSet &enchs, int32_t prior_penalty, int32_t durability);
    ItemStack(const Equipment &equip, const EnchSet &enchs, int32_t prior_penalty = 0);

    bool is_book() const;
    bool is_equipment() const;
};

using ItemCollection = std::vector<ItemStack>;
