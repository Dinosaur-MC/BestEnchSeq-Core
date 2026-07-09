#include "CompactAdapter.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace compact {

// ─── Helpers ────────────────────────────────────────────────────────────────

static int32_t penalty_cost(int8_t ppn) noexcept {
    return (1 << ppn) - 1;
}

// ─── Domain → compact ───────────────────────────────────────────────────────

Item from_domain(const ItemStack& item, const EnchReg& reg) {
    Item citem;
    citem.type = item.is_book() ? ItemType::Book : ItemType::Equip;
    citem.ppn = static_cast<int8_t>(item.prior_penalty);
    citem.dur = static_cast<int16_t>(item.durability);

    const size_t mask_size = reg.get_mask_size();
    citem.exc_mask.assign(mask_size, 0);
    citem.enchs.reserve(item.enchantments.size());

    // Sort EnchSet by id for deterministic compact representation
    // (EnchSet is a sorted set, iterate in order)
    int32_t total_lsum = 0;
    for (const auto& ench : item.enchantments) {
        int16_t eid = static_cast<int16_t>(ench.id);
        int16_t elv = static_cast<int16_t>(ench.level);

        citem.enchs.push_back({eid, elv});

        // OR in the exclusion mask for this enchantment id
        const auto& info_mask = reg[eid].exc_mask;
        for (size_t k = 0; k < mask_size; ++k)
            citem.exc_mask[k] |= info_mask[k];

        // lsum: sum of (level * multiplier) for all enchantments.
        // Uses book multiplier for books, equipment multiplier for equipment.
        int32_t mult = item.is_book()
            ? book_multiplier(reg.get_multiplier(eid))
            : reg.get_multiplier(eid);
        total_lsum += elv * mult;
    }

    citem.lsum = static_cast<int16_t>(total_lsum);
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
    // Rebuild EnchSet from flat ench list
    EnchSet ench_set;
    for (const auto& e : item.enchs)
        ench_set.emplace(e.id, e.level);

    if (item.type == ItemType::Book)
        return ItemStack(ench_set, item.ppn);
    else // Equip or Material
        return ItemStack(eq, ench_set, item.ppn, item.dur);
}

// ─── High-level adapter ─────────────────────────────────────────────────────

CompactInput prepare(const AlgorithmInput& input, const EnchReg& reg) {
    CompactInput ci;
    ci.equipment = input.target_item.equipment;

    // Build target equipment item (with initial enchantments)
    ItemStack start_item(ci.equipment, input.original_ench, 0);
    ci.items.reserve(1 + input.available_items.size());
    ci.items.push_back(from_domain(start_item, reg));

    // Add available books
    for (const auto& book : input.available_items)
        ci.items.push_back(from_domain(book, reg));

    return ci;
}

// ─── Cost estimation ────────────────────────────────────────────────────────

int32_t estimate_forge_cost(const Item& target, const Item& sacrifice, const EnchReg& reg) {
    int32_t cost = penalty_cost(target.ppn) + penalty_cost(sacrifice.ppn);

    bool sac_is_book = (sacrifice.type == ItemType::Book);
    for (const auto& e : sacrifice.enchs) {
        int32_t mult = sac_is_book
            ? book_multiplier(reg.get_multiplier(e.id))
            : reg.get_multiplier(e.id);
        cost += e.level * mult;
    }

    return cost;
}

} // namespace compact
