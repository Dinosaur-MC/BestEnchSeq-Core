#include "Item.h"

#include <stdexcept>

Item::Item() : equipment(std::nullopt), prior_penalty(0), durability(0) {}
Item::Item(const EnchSet &enchs, int32_t prior_penalty)
    : equipment(std::nullopt), enchantments(enchs), prior_penalty(prior_penalty), durability(0) {
    if (prior_penalty < 0)
        throw std::invalid_argument("Negative prior penalty");
}
Item::Item(
    const Equipment &equip, const EnchSet &enchs, int32_t prior_penalty, int32_t durability
)
    : equipment(equip), enchantments(enchs), prior_penalty(prior_penalty), durability(durability) {
    if (prior_penalty < 0 || durability < 0)
        throw std::invalid_argument("Negative prior penalty or durability");
}
Item::Item(const Equipment &equip, const EnchSet &enchs, int32_t prior_penalty)
    : equipment(equip), enchantments(enchs), prior_penalty(prior_penalty) {
    if (prior_penalty < 0)
        throw std::invalid_argument("Negative prior penalty");
    durability = equip.max_durability;
}

bool Item::is_book() const { return !equipment.has_value() && durability <= 0; }
bool Item::is_equipment() const { return equipment.has_value(); }
