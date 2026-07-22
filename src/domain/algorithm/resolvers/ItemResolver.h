#pragma once
#include "../types/Item.h"
#include <vector>

namespace algorithm {

/// Domain-level resolved input — output of ItemResolver,
/// consumed by CompactAdapter.
struct ResolvedInput {
    Item target_item;
    EnchSet source_ench;
    EnchSet target_ench;
    ItemCollection available_items;
};

/// Direct-mode input preprocessing.
///   1. Validates target enchantments are applicable to target equipment
///   2. Validates no exclusive_set conflicts among target enchantments
///   3. Computes diff = target_ench - source_ench
///   4. Generates graduated equipment books (items) for the diff
///
/// Throws std::invalid_argument on validation failure.
/// Domain-level only — compact conversion and strict validation are
/// handled later by CompactAdapter.
struct ItemResolver {
    static ResolvedInput resolve(
        const Item& target_item,
        const EnchSet& source_ench,
        const EnchSet& target_ench
    );
};

}; // namespace algorithm
