#pragma once

#include "domain/business/components/ItemProperties.h"
#include "domain/business/components/TagResolver.h"
#include "domain/business/registries/EnchantmentRegistry.h"

#include <string>
#include <unordered_map>

/// Registry-level, stateless `limited_level` calculator (B-T18).
///
/// Computes and back-fills `EnchInfo::limited_level` for EVERY enchantment in
/// the registry, uniformly across data sources (vanilla.json native / custom
/// native / datapack).  The data files carry only the raw fields
/// (`max_level` / `min_cost` / `supported_items` / `is_treasure`); this is the
/// single owner of the limited_level formula.
///
/// Fallback chain per enchantment (highest priority first):
///   1. `is_treasure`           → limited_level = 0  (not in #in_enchanting_table)
///   2. `min_cost_base > 0`     → compute from the cost formula + item enchantability
///   3. `limited_level_provided`→ keep the stored (legacy pre-computed) value
///   4. else                    → limited_level = max_level  (guarantee usability)
class LimitedLevelCalculator {
public:
    static void compute(EnchantmentRegistry& ench_reg,
                        const TagResolver& resolver,
                        const std::unordered_map<std::string, ItemProperty>& item_props);
};
