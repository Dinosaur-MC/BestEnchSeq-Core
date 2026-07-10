#include "ItemStack.h"

#include <stdexcept>

ItemStack::ItemStack() : equipment(std::nullopt), prior_penalty(0), durability(0) {}
ItemStack::ItemStack(const EnchSet &enchs, int32_t prior_penalty)
    : equipment(std::nullopt), enchantments(enchs), prior_penalty(prior_penalty), durability(0) {
    if (prior_penalty < 0)
        throw std::invalid_argument("Negative prior penalty");
}
ItemStack::ItemStack(
    const Equipment &equip, const EnchSet &enchs, int32_t prior_penalty, int32_t durability
)
    : equipment(equip), enchantments(enchs), prior_penalty(prior_penalty), durability(durability) {
    if (prior_penalty < 0 || durability < 0)
        throw std::invalid_argument("Negative prior penalty or durability");
}
ItemStack::ItemStack(const Equipment &equip, const EnchSet &enchs, int32_t prior_penalty)
    : equipment(equip), enchantments(enchs), prior_penalty(prior_penalty) {
    if (prior_penalty < 0)
        throw std::invalid_argument("Negative prior penalty");
    durability = equip.max_durability;
}

bool ItemStack::is_book() const { return !equipment.has_value() && durability <= 0; }
bool ItemStack::is_equipment() const { return equipment.has_value(); }
