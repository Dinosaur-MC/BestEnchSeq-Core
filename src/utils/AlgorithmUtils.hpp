#pragma once
#include "../algorithm/forge/IForgeEngine.h"
#include "../algorithm/ExecutionContext.h"
#include "../BESQTypes.h"
#include "ExpCalculator.hpp"
#include <cstddef>

// ─── Shared algorithm utilities ───
// Header-only; no .cpp needed.
//
// These functions were extracted from DFSAlgorithm (greedy_upper_bound,
// lower_bound, meets_target) so they can be shared across GreedyAlgorithm,
// DynamicPenaltyBalancing, HierarchicalMergeStrategy, and DFSAlgorithm.
namespace AlgorithmUtils {

// ─── Hash utilities ───

inline void hash_combine(size_t& seed, size_t v) noexcept {
    seed ^= v + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

// ─── Book-first merge ───
//
// Merges all books together first so penalty accumulates on the book stack
// instead of on the equipment. Then applies the combined book to the
// equipment in a single forge operation.
//
// This produces significantly better results than sequential forge-to-equipment
// because the equipment's prior_penalty stays at 0 for all but the final step.
//
// Returns the final equipment state, total cost, and per-step breakdown.
struct BookFirstMergeResult {
    ItemStack equipment;
    int32_t total_cost;
    EnchStepList steps;
};

inline BookFirstMergeResult book_first_merge(
    const ItemStack& equipment,
    const ItemCollection& books,
    const IForgeEngine& engine,
    ExecutionContext& ctx)
{
    BookFirstMergeResult result;
    result.total_cost = 0;

    if (books.empty()) {
        result.equipment = equipment;
        return result;
    }

    if (books.size() == 1) {
        // Single book: forge directly onto equipment
        auto [final_item, cost] = engine.forge(equipment, books[0]);
        result.total_cost = cost;
        result.steps.push_back({equipment, books[0], cost, ExpCalculator::level_to_exp(cost)});
        result.equipment = final_item;
        return result;
    }

    // Phase 1: Merge all books together
    // Start with the first book, then forge each subsequent book into it.
    // Using forge() here (not forge_into()) since we need the intermediate
    // step details for reporting and the return-to-variable approach is
    // clearer for the sequential merge pattern.
    ItemStack combined_book = books[0];
    auto [tmp_book, ph1_cost] = engine.forge(combined_book, books[1]);
    combined_book = tmp_book;
    result.total_cost += ph1_cost;
    result.steps.push_back({books[0], books[1], ph1_cost, ExpCalculator::level_to_exp(ph1_cost)});

    for (size_t k = 2; k < books.size(); ++k) {
        if (ctx.is_cancelled()) return result;
        ctx.wait_if_paused();

        auto [next_book, cost] = engine.forge(combined_book, books[k]);
        result.total_cost += cost;
        result.steps.push_back({combined_book, books[k], cost, ExpCalculator::level_to_exp(cost)});
        combined_book = next_book;
    }

    // Phase 2: Apply combined book to equipment
    auto [final_item, ph2_cost] = engine.forge(equipment, combined_book);
    result.total_cost += ph2_cost;
    result.steps.push_back({equipment, combined_book, ph2_cost, ExpCalculator::level_to_exp(ph2_cost)});
    result.equipment = final_item;

    return result;
}

// ─── Admissible heuristic ───
//
// Compute a lower bound on the remaining forge cost:
//   sum((target_level - current_level) * book_multiplier) for each target
//   enchantment not fully satisfied by `current`.
//
// Uses book multipliers (the smallest possible), ignores penalty costs,
// incompatibility, and the 39-level cap — so it always underestimates
// (or equals) the true remaining cost → admissible for A* and branch-and-bound.
inline int32_t admissible_heuristic(const EnchSet& current, const EnchSet& target) {
    int32_t h = 0;
    for (const Ench& e : target) {
        auto it = current.find(e);
        if (it == current.end()) {
            h += e.level * e.get_multiplier(true);  // book multiplier
        } else if (it->level < e.level) {
            h += (e.level - it->level) * e.get_multiplier(true);
        }
    }
    return h;
}

// ─── Goal check ───
//
// Returns true if `item` meets or exceeds all requirements of `target`:
//   - Same equipment type (pointer comparison — both reference the same
//     singleton from EquipmentRegistry)
//   - Every enchantment in target is present in item at ≥ target level
inline bool meets_target(const ItemStack& item, const ItemStack& target) {
    if (item.equipment != target.equipment)
        return false;
    for (const Ench& e : target.enchantments) {
        auto it = item.enchantments.find(e);
        if (it == item.enchantments.end() || it->level < e.level)
            return false;
    }
    return true;
}

} // namespace AlgorithmUtils
