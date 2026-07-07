#pragma once
#include "EnchSet.h"
#include "EquipmentType.h"

struct ItemStack {
    const EquipmentType *equipment;
    EnchSet enchantments;
    int32_t prior_penalty;
    int32_t durability;
    int32_t priority = 99;  // lower = more preferred (inventory mode)

  private:
    struct Cache {
        int32_t ench_eval_cost;
        int32_t total_eval_cost;
    } mutable _cache;

  public:
    ItemStack();
    ItemStack(const EnchSet &enchs, int32_t prior_penalty = 0);
    ItemStack(const EquipmentType *equipment, const EnchSet &enchs, int32_t prior_penalty, int32_t durability);
    ItemStack(const EquipmentType *equipment, const EnchSet &enchs, int32_t prior_penalty = 0);

    void update_cache() const;
    const Cache &get_cache() const;

    bool is_book() const;
    bool is_equipment() const;

    static int32_t get_penalty_cost(int32_t n);
    int32_t get_penalty_cost() const;
};

using ItemCollection = std::vector<ItemStack>;
