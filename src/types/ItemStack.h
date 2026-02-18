#pragma once
#include "EnchSet.h"
#include "Equipment.h"
#include <cstdint>

struct ItemStack {
    const Equipment *equipment_type;
    EnchSet enchantments;
    int32_t prior_penalty;
    int32_t durability;

  public:
    static int32_t get_penalty_cost(int32_t n);
    int32_t get_penalty_cost() const;
};
