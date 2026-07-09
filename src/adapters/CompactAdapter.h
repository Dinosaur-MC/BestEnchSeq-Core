#pragma once
#include "types/CompactedTypes.h"
#include "registries/CompactedRegistries.h"
#include "types/ItemStack.h"
#include "types/EnchSolution.h"
#include "registries/EnchantmentRegistry.h"
#include "algorithm/IAlgorithm.h"
#include "algorithm/forge/IForgeEngine.h"
#include <vector>

class CompactAdapter {
public:
    AlgorithmInput apply(
        const ItemStack& target_item,
        const EnchSet& original_ench,
        const ItemCollection& available_items,
        const ForgeConfig& config,
        const EnchantmentRegistry& global_registry
    );

    std::vector<EnchSolution> recall(
        const AlgorithmOutput& output,
        const AlgorithmInput& input,
        const EnchSet& original_ench,
        const ItemStack& target_item,
        const ItemCollection& available_items
    );

    static compact::Item from_domain(const ItemStack& item, const compact::EnchReg& reg);
    static ItemStack to_domain(const compact::Item& item, const Equipment& eq,
                               const compact::EnchReg& reg);
};
