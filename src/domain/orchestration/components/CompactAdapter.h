#pragma once
#include "domain/algorithm/types/Item.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/types/ResolverTypes.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/Item.h"
#include "domain/business/types/Solution.h"
#include <vector>

struct CompactAdapter {
    /// Convert domain-level resolved items to compact AlgorithmInput.
    /// @param target_item   Target equipment (with source enchantments)
    /// @param source_ench   Source enchantments already on the equipment
    /// @param target_ench   Desired final enchantments
    /// @param books         Graduated books (ResolverOutput from ItemResolver)
    /// @param target_eq     Business Equipment descriptor for the target item
    /// @param global_registry Full business enchantment registry
    static algorithm::AlgorithmInput apply(
        const algorithm::Item& target_item,
        const algorithm::EnchSet& source_ench,
        const algorithm::EnchSet& target_ench,
        const algorithm::ResolverOutput& books,
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
