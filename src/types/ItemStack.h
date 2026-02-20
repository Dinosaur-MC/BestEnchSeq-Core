#pragma once
#include "EnchSet.h"
#include "Equipment.h"

struct ItemStack {
    const Equipment *equipment;
    EnchSet enchantments;
    int32_t prior_penalty;
    int32_t durability;

  private:
    struct Cache {
        int32_t ench_eval_cost;
        int32_t total_eval_cost;
    } mutable _cache;

  public:
    ItemStack();
    ItemStack(const EnchSet &enchs, int32_t prior_penalty = 0);
    ItemStack(const Equipment *equipment, const EnchSet &enchs, int32_t prior_penalty, int32_t durability);
    ItemStack(const Equipment *equipment, const EnchSet &enchs, int32_t prior_penalty = 0);

    void update_cache() const;
    const Cache &get_cache() const;

    bool is_book() const;
    bool is_equipment() const;
    int32_t get_multiplier_index() const;

    static int32_t get_penalty_cost(int32_t n);
    int32_t get_penalty_cost() const;
};

using ItemCollection = std::vector<ItemStack>;
