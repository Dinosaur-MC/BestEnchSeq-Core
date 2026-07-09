#include "CompactDynamicPenaltyBalancing.h"
#include "utils/AlgorithmUtils.hpp"
#include "utils/CompactAdapter.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

void CompactDynamicPenaltyBalancing::execute(
    const AlgorithmInput& input, ExecutionContext& ctx)
{
    ctx.report_progress(0.0, ProgressStatus::Starting);

    //── Boundary: prepare compact data ────────────────────────────────────
    auto& ench_reg = compact::EnchReg::get_instance();
    ench_reg.init(EnchantmentRegistry::get_instance(), *input.target_item.equipment);
    auto ci = compact::prepare(input, ench_reg);
    auto& items = ci.items;

    // Quick check: goal already met?
    if (AlgorithmUtils::meets_target(
            compact::to_domain(items[0], ci.equipment), input.target_item))
    {
        ctx.report_solution_found({});
        ctx.report_progress(1.0, ProgressStatus::GoalAlreadyMet);
        return;
    }

    std::vector<compact::EnchStep> compact_steps;
    const size_t initial_count = items.size();

    // Main loop: keep merging until only the final equipment remains
    while (items.size() > 1) {
        if (ctx.is_cancelled()) return;
        ctx.wait_if_paused();

        // Find the best pair to merge (closest penalty → cheapest → book-book)
        size_t best_i = 0, best_j = 0;
        int32_t best_pen_diff = INT32_MAX;
        int32_t best_est_cost = INT32_MAX;
        bool best_both_books = false;
        bool found = false;

        for (size_t i = 0; i < items.size(); ++i) {
            for (size_t j = 0; j < items.size(); ++j) {
                if (i == j) continue;
                if (!compact::CompactForgeEngine::is_forgeable(items[i], items[j]))
                    continue;

                int32_t pen_diff = std::abs(static_cast<int32_t>(items[i].ppn)
                                          - static_cast<int32_t>(items[j].ppn));
                int32_t est = compact::estimate_forge_cost(items[i], items[j], ench_reg);
                bool both_books = (items[i].type == compact::ItemType::Book
                                && items[j].type == compact::ItemType::Book);

                if (!found || pen_diff < best_pen_diff) {
                    best_pen_diff = pen_diff;
                    best_est_cost = est;
                    best_both_books = both_books;
                    best_i = i; best_j = j;
                    found = true;
                } else if (pen_diff == best_pen_diff) {
                    if (est < best_est_cost) {
                        best_est_cost = est;
                        best_both_books = both_books;
                        best_i = i; best_j = j;
                    } else if (est == best_est_cost && both_books && !best_both_books) {
                        best_both_books = true;
                        best_i = i; best_j = j;
                    }
                }
            }
        }

        if (!found) break;

        // Save compact copies before forge
        compact::Item saved_i = items[best_i];
        compact::Item saved_j = items[best_j];

        int32_t step_cost = _forge_engine.forge_into(items[best_i], items[best_j], ench_reg);

        compact_steps.push_back({
            std::move(saved_i), std::move(saved_j), step_cost
        });

        items.erase(items.begin() + best_j);

        double progress = 1.0 - static_cast<double>(items.size()) / initial_count;
        ctx.report_progress(progress, ProgressStatus::MergingGroups);
    }

    //── Boundary: convert to domain for output ───────────────────────────
    auto steps = compact::to_domain(compact_steps.begin(), compact_steps.end(), ci.equipment);
    ctx.report_solution_found(steps);
    ctx.report_progress(1.0, ProgressStatus::Complete);
}
