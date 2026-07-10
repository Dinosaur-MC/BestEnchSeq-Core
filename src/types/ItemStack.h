#pragma once
#include "EnchSet.h"
#include "Equipment.h"

/// Forgeable item stack — pure data container.
struct ItemStack {
    const Equipment *equipment;
    EnchSet enchantments;
    int32_t prior_penalty;
    int32_t durability;
    int32_t priority = 99;  // lower = more preferred (inventory mode)

    ItemStack();
    ItemStack(const EnchSet &enchs, int32_t prior_penalty = 0);
    ItemStack(const Equipment *equipment, const EnchSet &enchs, int32_t prior_penalty, int32_t durability);
    ItemStack(const Equipment *equipment, const EnchSet &enchs, int32_t prior_penalty = 0);

    bool is_book() const;
    bool is_equipment() const;
};

using ItemCollection = std::vector<ItemStack>;
