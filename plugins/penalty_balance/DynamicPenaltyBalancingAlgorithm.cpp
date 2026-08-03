#include "DynamicPenaltyBalancingAlgorithm.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/resolvers/IResolver.h"
#include <chrono>
#include <cstdint>
#include <vector>

namespace algorithm {

using namespace algorithm;

void DynamicPenaltyBalancingAlgorithm::execute(const AlgorithmInput &input, ExecutionContext& ctx)
{
    _forge_engine.set_config(input.config.forge);
    _ench_reg = &input.registry;
    auto items = get_resolver()->resolve(input);
    normalize_base_equipment(items);
    const auto& reg = input.registry;
    const auto& target = input.target;
    ctx.report_progress(0, ProgressStatus::Starting);

    auto start = std::chrono::steady_clock::now();

    std::vector<Item> mut_items = items;

    // Quick check: goal already met?
    {
        bool met = true;
        for (const auto& t : target.enchs) {
            if (!mut_items[0].enchs.contains(t.id()) || mut_items[0].enchs[t.id()] < t.level()) {
                met = false;
                break;
            }
        }
        if (met) {
            ctx.report_solution({});
            ctx.report_progress(100, ProgressStatus::GoalAlreadyMet);
            return;
        }
    }

    std::vector<EnchStep> compact_steps;
    const size_t initial_count = mut_items.size();

    while (mut_items.size() > 1) {
        if (ctx.is_cancelled()) return;
        ctx.wait_if_paused();

        size_t best_i = 0, best_j = 0;
        int32_t best_pen_diff = INT32_MAX;
        int32_t best_est_cost = INT32_MAX;
        bool best_both_books = false;
        bool found = false;

        for (size_t i = 0; i < mut_items.size(); ++i) {
            for (size_t j = 0; j < mut_items.size(); ++j) {
                if (i == j) continue;
                if (!_forge_engine.is_forgeable(mut_items[i], mut_items[j]))
                    continue;

                int32_t pen_diff = std::abs(static_cast<int32_t>(mut_items[i].ppn)
                                          - static_cast<int32_t>(mut_items[j].ppn));
                int32_t est = _forge_engine.estimate_forge_cost(mut_items[i], mut_items[j], reg);
                bool both_books = (mut_items[i].type == ItemType::Book
                                && mut_items[j].type == ItemType::Book);

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

        // Reverse-orientation guard (book-book only): the chosen direction would
        // discard a still-needed target enchant (ForgeEngine::forge_into drops
        // it), but the reverse direction would keep it → swap so the wasteful
        // book is the sacrifice.  Safe because book-book pairs have no fixed
        // forge root; equipment-involving pairs keep the equipment as base (a
        // wasteful drop there means the base's conflict is permanent → genuinely
        // unreachable).
        if (merge_wastes_target(mut_items[best_i], mut_items[best_j], target, reg) &&
            mut_items[best_i].type == ItemType::Book && mut_items[best_j].type == ItemType::Book &&
            !merge_wastes_target(mut_items[best_j], mut_items[best_i], target, reg)) {
            std::swap(best_i, best_j);
        }

        Item saved_i = mut_items[best_i];
        Item saved_j = mut_items[best_j];

        int32_t step_cost = _forge_engine.forge_into(mut_items[best_i], mut_items[best_j], reg);
        ctx.incr_steps_forged();

        Item result = mut_items[best_i];  // forge_into mutates the target in place
        compact_steps.push_back({
            std::move(saved_i), std::move(saved_j), std::move(result), step_cost
        });

        {
            // `max_solutions` is a SOLUTION-count cap (same semantics as dfs/astar:
            // stop after N solutions found), NOT a merge-step cap.  This greedy
            // produces exactly one solution, and the loop is already bounded by
            // the shrinking item count, max_search_time and cooperative
            // cancellation — so misreading it as a step limit here would abort
            // multi-step targets early (e.g. default --solutions 1 → needless
            // CompleteNoSolution).  Nothing to cap.
            const auto& sc = input.config.search;
            if (sc.max_search_time.count() > 0) {
                auto elapsed = std::chrono::steady_clock::now() - start;
                if (elapsed > sc.max_search_time) break;
            }
        }

        mut_items.erase(mut_items.begin() + best_j);

        uint8_t progress = 100 - static_cast<uint8_t>(mut_items.size()) * 100 / static_cast<uint8_t>(initial_count);
        ctx.report_progress(progress, ProgressStatus::MergingGroups);
    }

    // Verify target achieved
    {
        bool met = true;
        for (const auto& t : target.enchs) {
            if (!mut_items[0].enchs.contains(t.id()) || mut_items[0].enchs[t.id()] < t.level()) {
                met = false;
                break;
            }
        }
        if (!met) {
            _diag.status = "CompleteNoSolution";
            ctx.set_exit_diagnostics(_diag);
            ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
            return;
        }
    }

    _diag.status = "Complete";
    ctx.set_exit_diagnostics(_diag);

    ctx.report_solution(compact_steps);
    ctx.report_progress(100, ProgressStatus::Complete);
}


// ─── evaluate ──────────────────────────────────────────────────────────────────

double DynamicPenaltyBalancingAlgorithm::evaluate(int16_t ench_count) const noexcept {
    (void)ench_count;
    // O(n²) per merge step — small constant, always fast.
    return 0;
}


} // namespace algorithm
