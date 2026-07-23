#pragma once
#include "domain/algorithm/types/Item.h"
#include "domain/algorithm/types/Solution.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/resolvers/ItemResolver.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/Item.h"
#include "domain/business/types/Solution.h"
#include <vector>

struct CompactAdapter {
    /// Convert domain-level resolved input to compact AlgorithmInput.
    /// @param resolved      Algorithm-domain resolved input (ItemResolver output)
    /// @param target_eq     Business Equipment descriptor for the target item
    /// @param global_registry Full business enchantment registry
    static algorithm::AlgorithmInput apply(
        const algorithm::ResolvedInput& resolved,
        const ::Equipment& target_eq,
        const EnchantmentRegistry& global_registry
    );

    /// Convert compact algorithm output back to domain Solution list.
    static std::vector<Solution> recall(
        const algorithm::AlgorithmOutput& output,
        const algorithm::AlgorithmInput& input,
        const EnchSet& original_ench,
        const Item& target_item,
        const ItemCollection& available_items
    );

    /// Single-item conversion helpers.
    static algorithm::Item from_domain(const Item& item, const algorithm::EnchReg& reg);
    static Item to_domain(const algorithm::Item& item, const algorithm::EnchReg& reg);
};
