#include "DynamicPenaltyBalancing.h"
#include "utils/AlgorithmUtils.hpp"
#include "utils/ExpCalculator.hpp"

#include <cstdint>
#include <vector>

void DynamicPenaltyBalancing::execute(
    const AlgorithmInput& input, ExecutionContext& ctx)
{
    ctx.report_progress(0.0, "starting penalty balancing");

    // Build initial item list: equipment + books
    ItemStack equipment(
        input.target_item.equipment,
        input.original_ench,
        0
    );

    std::vector<ItemStack> items;
    items.reserve(1 + input.available_items.size());
    items.push_back(std::move(equipment));
    items.insert(items.end(),
                 input.available_items.begin(),
                 input.available_items.end());

    // Quick check: goal already met?
    if (AlgorithmUtils::meets_target(items[0], input.target_item)) {
        ctx.report_solution_found({});
        ctx.report_progress(1.0, "goal already met");
        return;
    }

    // Track which index is the equipment (items[0] always)
    EnchStepList steps;
    int32_t total_cost = 0;
    const size_t initial_count = items.size();

    // Main loop: keep merging until only the final equipment remains
    while (items.size() > 1) {
        if (ctx.is_cancelled()) return;
        ctx.wait_if_paused();

        // Find the best pair to merge
        size_t best_i = 0, best_j = 0;
        int32_t best_penalty_diff = INT32_MAX;
        int32_t best_est_cost = INT32_MAX;
        bool best_both_books = false;
        bool found = false;

        for (size_t i = 0; i < items.size(); ++i) {
            for (size_t j = 0; j < items.size(); ++j) {
                if (i == j) continue;

                if (!_forge_engine.is_forgeable(items[i], items[j]))
                    continue;

                int32_t pen_diff = std::abs(items[i].prior_penalty
                                          - items[j].prior_penalty);
                int32_t est_cost = ItemStack::get_penalty_cost(items[i].prior_penalty)
                                 + ItemStack::get_penalty_cost(items[j].prior_penalty);
                for (const Ench& e : items[j].enchantments)
                    est_cost += e.level * e.get_multiplier(items[j].is_book());
                bool both_books = (!items[i].is_equipment() && !items[j].is_equipment());

                // 3-level sort
                if (!found || pen_diff < best_penalty_diff) {
                    best_penalty_diff = pen_diff;
                    best_est_cost = est_cost;
                    best_both_books = both_books;
                    best_i = i; best_j = j;
                    found = true;
                } else if (pen_diff == best_penalty_diff) {
                    if (est_cost < best_est_cost) {
                        best_est_cost = est_cost;
                        best_both_books = both_books;
                        best_i = i; best_j = j;
                    } else if (est_cost == best_est_cost && both_books && !best_both_books) {
                        best_both_books = true;
                        best_i = i; best_j = j;
                    }
                }
            }
        }

        if (!found) {
            // No forgeable pair — should not happen with valid input
            break;
        }

        // Execute the forge
        ItemStack saved_i = items[best_i];
        ItemStack saved_j = items[best_j];

        int32_t step_cost = _forge_engine.forge_into(items[best_i], items[best_j]);
        total_cost += step_cost;

        steps.push_back({
            saved_i,
            saved_j,
            step_cost,
            ExpCalculator::level_to_exp(step_cost)
        });

        // Remove the sacrifice. Note: forge_into() already modified
        // items[best_i] in-place, so we only need to remove items[best_j].
        items.erase(items.begin() + best_j);

        double progress = 1.0 - static_cast<double>(items.size()) / initial_count;
        ctx.report_progress(progress, "penalty balancing: " + std::to_string(items.size()) + " items remaining");
    }

    ctx.report_solution_found(steps);
    ctx.report_progress(1.0, "penalty balancing complete (cost: " + std::to_string(total_cost) + ")");
}
