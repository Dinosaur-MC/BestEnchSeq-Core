#include "ItemStack.h"

#include <stdexcept>

ItemStack::ItemStack() : equipment(nullptr), prior_penalty(0), durability(0), _cache({0, 0}) {}
ItemStack::ItemStack(const EnchSet &enchs, int32_t prior_penalty)
    : equipment(nullptr), enchantments(enchs), prior_penalty(prior_penalty), durability(0) {
    if (prior_penalty < 0)
        throw std::invalid_argument("Negative prior penalty");
    update_cache();
}
ItemStack::ItemStack(
    const Equipment *equipment, const EnchSet &enchs, int32_t prior_penalty, int32_t durability
)
    : equipment(equipment), prior_penalty(prior_penalty), durability(durability), enchantments(enchs) {
    if (prior_penalty < 0 || durability < 0)
        throw std::invalid_argument("Negative prior penalty or durability");
    update_cache();
}
ItemStack::ItemStack(const Equipment *equipment, const EnchSet &enchs, int32_t prior_penalty)
    : equipment(equipment), prior_penalty(prior_penalty), enchantments(enchs) {
    if (prior_penalty < 0)
        throw std::invalid_argument("Negative prior penalty");
    if (equipment != nullptr)
        durability = equipment->max_durability;
    update_cache();
}

void ItemStack::update_cache() const {
    if (enchantments.empty()) {
        _cache = {0, 0};
        return;
    }

    int32_t ench_eval_cost = 0;
    for (const Ench &e : enchantments) {
        int32_t multiplier = e.get_multiplier(get_multiplier_index());
        if (multiplier > 0)
            ench_eval_cost += multiplier * e.level;
    }

    _cache.ench_eval_cost  = ench_eval_cost;
    _cache.total_eval_cost = ench_eval_cost + get_penalty_cost();
}
const ItemStack::Cache &ItemStack::get_cache() const { return _cache; }

bool ItemStack::is_book() const { return equipment == nullptr && durability <= 0; }
bool ItemStack::is_equipment() const { return equipment != nullptr; }
int32_t ItemStack::get_multiplier_index() const { return is_book() ? 0 : 1; }

int32_t ItemStack::get_penalty_cost(int32_t n) { return (1 << n) - 1; }
int32_t ItemStack::get_penalty_cost() const { return (1 << prior_penalty) - 1; }
