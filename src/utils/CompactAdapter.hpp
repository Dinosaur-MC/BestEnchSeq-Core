#pragma once
#include "types/CompactedTypes.h"
#include "registries/CompactedRegistries.h"
#include "types/ItemStack.h"
#include "types/EnchSolution.h"
#include "types/AlgorithmInput.h"
#include "utils/CompactForgeUtils.hpp"
#include <iterator>
#include <vector>

namespace compact {

// ─── Domain → compact conversions ───────────────────────────────────────────

Item from_domain(const ItemStack& item, const EnchReg& reg);

std::vector<Item> from_domain(const std::vector<ItemStack>& items, const EnchReg& reg);

// ─── Compact → domain conversions ───────────────────────────────────────────

ItemStack to_domain(const Item& item, const Equipment* eq);

EnchSolution::EnchStep to_domain(const EnchStep& step, const Equipment* eq);

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
    std::vector<Item> items;
    const Equipment* equipment;
};

CompactInput prepare(const AlgorithmInput& input, const EnchReg& reg);

} // namespace compact
