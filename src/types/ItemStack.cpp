#include "ItemStack.h"

#include <stdexcept>

ItemStack::ItemStack() : equipment(nullptr), prior_penalty(0), durability(0) {}
ItemStack::ItemStack(const EnchSet &enchs, int32_t prior_penalty)
    : equipment(nullptr), enchantments(enchs), prior_penalty(prior_penalty), durability(0) {
    if (prior_penalty < 0)
        throw std::invalid_argument("Negative prior penalty");
}
ItemStack::ItemStack(
    const Equipment *equipment, const EnchSet &enchs, int32_t prior_penalty, int32_t durability
)
    : equipment(equipment), prior_penalty(prior_penalty), durability(durability), enchantments(enchs) {
    if (prior_penalty < 0 || durability < 0)
        throw std::invalid_argument("Negative prior penalty or durability");
}
ItemStack::ItemStack(const Equipment *equipment, const EnchSet &enchs, int32_t prior_penalty)
    : equipment(equipment), prior_penalty(prior_penalty), enchantments(enchs) {
    if (prior_penalty < 0)
        throw std::invalid_argument("Negative prior penalty");
    if (equipment != nullptr)
        durability = equipment->max_durability;
}

bool ItemStack::is_book() const { return equipment == nullptr && durability <= 0; }
bool ItemStack::is_equipment() const { return equipment != nullptr; }
