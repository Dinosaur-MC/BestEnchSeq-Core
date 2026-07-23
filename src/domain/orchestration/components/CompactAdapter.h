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
    /// Build AlgorithmInput from domain-level types.
    /// items[0] = equipment with source enchantments.
    /// extra_items are placed into items[1..] and later processed by
    /// IAlgorithm::resolve() (which generates books or filters by priority).
    static algorithm::AlgorithmInput apply(
        const algorithm::Item& target_item,
        const algorithm::EnchSet& source_ench,
        const algorithm::EnchSet& target_ench,
        const algorithm::ItemCollection& extra_items,
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
