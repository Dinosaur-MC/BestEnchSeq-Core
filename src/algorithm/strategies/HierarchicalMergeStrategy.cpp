#include "HierarchicalMergeStrategy.h"
#include "utils/ExpCalculator.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

int32_t HierarchicalMergeStrategy::effective_multiplier(const ItemStack& item) {
    int32_t max_mult = 1;
    for (const Ench& e : item.enchantments) {
        int32_t m = e.get_multiplier(true);  // book multiplier
        if (m > max_mult) max_mult = m;
    }
    return max_mult;
}

ItemStack HierarchicalMergeStrategy::merge_group(
    std::vector<ItemStack>& group,
    EnchStepList& steps,
    ExecutionContext& ctx)
{
    if (group.empty())
        return {};

    if (group.size() == 1)
        return group[0];

    // Use penalty-balancing-like approach: merge within the group
    while (group.size() > 1) {
        if (ctx.is_cancelled()) return group[0];
        ctx.wait_if_paused();

        // Find the pair with closest penalty
        size_t best_i = 0, best_j = 1;
        int32_t best_diff = std::abs(group[0].prior_penalty - group[1].prior_penalty);

        for (size_t i = 0; i < group.size(); ++i) {
            for (size_t j = i + 1; j < group.size(); ++j) {
                if (!_forge_engine.is_forgeable(group[i], group[j]) &&
                    !_forge_engine.is_forgeable(group[j], group[i]))
                    continue;

                int32_t diff = std::abs(group[i].prior_penalty - group[j].prior_penalty);
                if (diff < best_diff) {
                    best_diff = diff;
                    best_i = i; best_j = j;
                }
            }
        }

        // Determine forge direction: pick the forgeable ordering
        size_t base_idx, sac_idx;
        if (_forge_engine.is_forgeable(group[best_i], group[best_j])) {
            base_idx = best_i; sac_idx = best_j;
        } else {
            base_idx = best_j; sac_idx = best_i;
        }

        ItemStack saved_base = group[base_idx];
        ItemStack saved_sac  = group[sac_idx];

        int32_t cost = _forge_engine.forge_into(group[base_idx], group[sac_idx]);

        steps.push_back({saved_base, saved_sac, cost, ExpCalculator::level_to_exp(cost)});

        group.erase(group.begin() + sac_idx);
    }

    return group[0];
}

void HierarchicalMergeStrategy::execute(
    const AlgorithmInput& input, ExecutionContext& ctx)
{
    ctx.report_progress(0.0, "starting hierarchical merge");

    ItemStack equipment(
        input.target_item.equipment,
        input.original_ench,
        0
    );

    if (input.available_items.empty()) {
        ctx.report_solution_found({});
        ctx.report_progress(1.0, "goal already met");
        return;
    }

    EnchStepList steps;

    // Phase 1: Optional dedup — merge same-enchantment books
    // when there are more than 7 items to reduce search complexity.
    std::vector<ItemStack> books = input.available_items;

    if (books.size() > 7) {
        // Merge same-enchantment books together
        std::unordered_map<int32_t, std::vector<size_t>> ench_to_books;
        for (size_t i = 0; i < books.size(); ++i) {
            // Assume single-enchantment books for dedup
            if (books[i].enchantments.size() == 1) {
                int32_t eid = books[i].enchantments.begin()->id;
                ench_to_books[eid].push_back(i);
            }
        }

        // For each enchantment with multiple books, merge them
        for (auto& [eid, indices] : ench_to_books) {
            if (indices.size() < 2) continue;

            // Sort indices descending so erasing doesn't shift unprocessed indices
            std::sort(indices.begin(), indices.end(), std::greater<>{});

            // Merge all books of the same enchantment into one
            auto& base = books[indices.back()];  // first book in original order
            for (size_t idx_idx = 1; idx_idx < indices.size(); ++idx_idx) {
                size_t sac_idx = indices[idx_idx];
                if (sac_idx >= books.size()) continue;  // already erased
                if (!_forge_engine.is_forgeable(base, books[sac_idx]))
                    continue;

                ItemStack saved_base = base;
                ItemStack saved_sac  = books[sac_idx];

                int32_t cost = _forge_engine.forge_into(base, books[sac_idx]);
                steps.push_back({saved_base, saved_sac, cost, ExpCalculator::level_to_exp(cost)});

                // Erase — since indices are sorted descending, this is safe
                // (all remaining indices are smaller)
                if (sac_idx < books.size())
                    books.erase(books.begin() + sac_idx);
            }
        }
    }

    // Phase 2: Group books by effective multiplier tier
    std::vector<ItemStack> low_group;   // mult 1
    std::vector<ItemStack> mid_group;   // mult 2
    std::vector<ItemStack> high_group;  // mult 3+

    for (auto& book : books) {
        int32_t mult = effective_multiplier(book);
        if (mult <= 1)
            low_group.push_back(std::move(book));
        else if (mult <= 2)
            mid_group.push_back(std::move(book));
        else
            high_group.push_back(std::move(book));
    }

    ctx.report_progress(0.3, "phase 2: merging within groups");

    // Merge each group into a single book
    ItemStack low_merged  = merge_group(low_group, steps, ctx);
    ItemStack mid_merged  = merge_group(mid_group, steps, ctx);
    ItemStack high_merged = merge_group(high_group, steps, ctx);

    // Collect non-empty group results, ordered by multiplier (low → mid → high)
    struct GroupResult { ItemStack book; int32_t mult; };
    std::vector<GroupResult> group_results;

    if (!low_merged.enchantments.empty())
        group_results.push_back({std::move(low_merged), 1});
    if (!mid_merged.enchantments.empty())
        group_results.push_back({std::move(mid_merged), 2});
    if (!high_merged.enchantments.empty())
        group_results.push_back({std::move(high_merged), 3});

    // Phase 3: Merge groups together (low multiplier first), then apply to equipment
    ctx.report_progress(0.6, "phase 3: applying to equipment");

    if (group_results.empty()) {
        ctx.report_solution_found(steps);
        ctx.report_progress(1.0, "hierarchical merge complete");
        return;
    }

    // Merge all group books into one combined stack (book-to-book merges)
    // to minimize equipment penalty accumulation.
    ItemStack combined = group_results[0].book;
    for (size_t g = 1; g < group_results.size(); ++g) {
        if (ctx.is_cancelled()) return;
        ctx.wait_if_paused();

        auto& next_book = group_results[g].book;
        if (!_forge_engine.is_forgeable(combined, next_book))
            continue;

        ItemStack saved_base = combined;
        int32_t cost = _forge_engine.forge_into(combined, next_book);
        steps.push_back({saved_base, next_book, cost, ExpCalculator::level_to_exp(cost)});
    }

    // Apply combined book to equipment
    if (_forge_engine.is_forgeable(equipment, combined)) {
        ItemStack saved_equip = equipment;
        int32_t cost = _forge_engine.forge_into(equipment, combined);
        steps.push_back({saved_equip, combined, cost, ExpCalculator::level_to_exp(cost)});
    }

    ctx.report_solution_found(steps);
    ctx.report_progress(1.0, "hierarchical merge complete");
}
