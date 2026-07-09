#pragma once
#include "types/CompactedTypes.h"
#include "registries/CompactedRegistries.h"
#include "types/ItemStack.h"
#include "algorithm/IAlgorithm.h"
#include <iterator>
#include <vector>

namespace compact {

// ─── Domain → compact conversions ───────────────────────────────────────────

/// Convert a single ItemStack to compact::Item.
/// The equipment pointer is NOT stored in the compact item (kept separately).
Item from_domain(const ItemStack& item, const EnchReg& reg);

/// Convert a vector of ItemStacks to compact items.
std::vector<Item> from_domain(const std::vector<ItemStack>& items, const EnchReg& reg);

// ─── Compact → domain conversions ───────────────────────────────────────────

/// Convert a compact::Item back to ItemStack.
/// @param item  Compact item (Book or Equip)
/// @param eq    Equipment pointer (nullptr for books; from original AlgorithmInput for equipment)
ItemStack to_domain(const Item& item, const Equipment* eq);

/// Convert a compact::EnchStep (base, sacrifice, cost) to domain EnchSolution::EnchStep.
/// The equipment pointer from the original AlgorithmInput is needed for ItemStack
/// reconstruction of equipment-type items.
EnchSolution::EnchStep to_domain(const EnchStep& step, const Equipment* eq);

/// Convert a range of compact::EnchStep to a domain EnchStepList.
template <typename Iter>
EnchStepList to_domain(Iter begin, Iter end, const Equipment* eq) {
    EnchStepList result;
    result.reserve(static_cast<size_t>(std::distance(begin, end)));
    for (auto it = begin; it != end; ++it)
        result.push_back(to_domain(*it, eq));
    return result;
}

// ─── High-level input adapter ───────────────────────────────────────────────

struct CompactInput {
    std::vector<Item> items;   // items[0] = target equipment, rest = available books
    const Equipment* equipment; // original equipment pointer for output
};

/// Build compact items from AlgorithmInput.
CompactInput prepare(const AlgorithmInput& input, const EnchReg& reg);

// ─── Cost estimation helper ─────────────────────────────────────────────────

/// Compute the book multiplier for an enchantment (JE rule: max(1, mul >> 1)).
inline int32_t book_multiplier(int32_t equip_mult) noexcept {
    return std::max(1, equip_mult >> 1);
}

/// Estimate forge cost: penalty cost + sum of sacrifice enchantment costs.
/// Used for pair sorting / heuristic (not actual forge cost).
int32_t estimate_forge_cost(const Item& target, const Item& sacrifice, const EnchReg& reg);

} // namespace compact
