#include "adapters/CompactAdapter.h"
#include "utils/ExpCalculator.hpp"

namespace compact {

// ─── Domain → compact ───────────────────────────────────────────────────────

Item from_domain(const ItemStack& item, const EnchReg& reg) {
    Item citem;
    citem.type = item.is_book() ? ItemType::Book : ItemType::Equip;
    citem.ppn = static_cast<int8_t>(item.prior_penalty);
    citem.dur = static_cast<int16_t>(item.durability);

    citem.enchs.reserve(item.enchantments.size());

    // Domain EnchSet iterates in sorted order — insert maintains canonical order
    for (const auto& ench : item.enchantments) {
        int16_t eid = static_cast<int16_t>(ench.id);
        int16_t elv = static_cast<int16_t>(ench.level);
        citem.enchs.insert({eid, elv});
    }

    return citem;
}

std::vector<Item> from_domain(const std::vector<ItemStack>& items, const EnchReg& reg) {
    std::vector<Item> result;
    result.reserve(items.size());
    for (const auto& item : items)
        result.push_back(from_domain(item, reg));
    return result;
}

// ─── Compact → domain ───────────────────────────────────────────────────────

ItemStack to_domain(const Item& item, const Equipment* eq) {
    // Rebuild domain ::EnchSet from compact ench list
    ::EnchSet ench_set;
    for (const auto& e : item.enchs)
        ench_set.emplace(e.id, e.level);

    if (item.type == ItemType::Book)
        return ItemStack(ench_set, item.ppn);
    else // Equip or Material
        return ItemStack(eq, ench_set, item.ppn, item.dur);
}

EnchSolution::EnchStep to_domain(const EnchStep& step, const Equipment* eq) {
    return {
        to_domain(step.base, eq),
        to_domain(step.sacrifice, eq),
        step.cost,
        ExpCalculator::level_to_exp(step.cost)
    };
}

} // namespace compact
