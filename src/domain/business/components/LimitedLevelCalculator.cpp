#include "LimitedLevelCalculator.h"
#include "domain/business/types/EnchInfo.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

/// Max enchanting power for an item with the given enchantability.
/// Mirrors the extractor's `calc_limited_level` / the former
/// `parser_detail::compute_limited_level`:
///   power = round((30 + 1 + 2·⌊e/4⌋)·1.15)
int32_t max_power(int32_t enchantability) {
    if (enchantability <= 0)
        return 0;
    double base = 30.0;
    double added = 1.0 + 2.0 * (static_cast<double>(enchantability) / 4.0);
    return static_cast<int32_t>(std::round((base + added) * 1.15));
}

} // namespace

void LimitedLevelCalculator::compute(EnchantmentRegistry& ench_reg,
                                     const TagResolver& resolver,
                                     const std::unordered_map<std::string, ItemProperty>& item_props) {
    for (auto& info : ench_reg) {
        // ── 1. Treasure → 0 (not in the enchanting-table pool #in_enchanting_table).
        if (info.is_treasure) {
            info.limited_level = 0;
            continue;
        }

        // ── 2. min_cost present → compute from the cost formula.
        //    minCost(l) = base + per_level·(l−1); highest reachable l is
        //    clamped to max_level.  The `per_level > 0` guard keeps a 0-cost
        //    flat enchant from dividing by zero.
        if (info.min_cost_base > 0 && info.min_cost_per_level > 0) {
            int32_t best = 0;
            for (const auto& item_nsid : info.supported_items) {
                // Expand `#tag` references to concrete item ids; a concrete
                // item id passes through unchanged (TagResolver::resolve).
                auto concrete = resolver.resolve(item_nsid.str());
                for (const auto& item : concrete) {
                    std::string bare = item;
                    auto colon = bare.find(':');
                    if (colon != std::string::npos)
                        bare = bare.substr(colon + 1);

                    auto it = item_props.find(bare);
                    if (it == item_props.end() || it->second.enchantability <= 0)
                        continue; // unknown item / no enchantability → no contribution

                    int32_t power = max_power(it->second.enchantability);
                    if (power >= info.min_cost_base) {
                        int32_t lvl = (power - info.min_cost_base) / info.min_cost_per_level + 1;
                        lvl = std::min(lvl, info.max_level);
                        best = std::max(best, lvl);
                    }
                }
            }
            // No contributing item → 1 (conservative, per spec).
            info.limited_level = std::max<int32_t>(1, best);
            continue;
        }

        // ── 3. Legacy pre-computed hint → keep the stored value.
        if (info.limited_level_provided)
            continue;

        // ── 4. Guarantee usability.
        info.limited_level = info.max_level;
    }
}
