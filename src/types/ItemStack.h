#pragma once
#include "EnchSet.h"
#include <cstdint>
#include <vector>

struct ItemStack {
    EnchSet enchantments;
    int32_t prior_penalty;
    int32_t durability;

  public:
    bool is_book() const;
    bool is_equipment() const;

    static int32_t get_penalty_cost(int32_t n);
    int32_t get_penalty_cost() const;
};

using ItemCollection = std::vector<ItemStack>;
