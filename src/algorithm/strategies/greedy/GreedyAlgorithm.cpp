#include "algorithm/strategies/greedy/GreedyAlgorithm.h"
#include "algorithm/ExecutionContext.h"
#include "algorithm/components/SearchUtils.h"
#include <chrono>
#include <algorithm>

using compact::Item;
using compact::EnchStep;
using compact::EnchReg;

void GreedyAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    _forge_engine.set_config(input.config);
    _target = input.target;
    const auto& items = input.items;
    const auto& reg = input.ench_reg;
    ctx.report_progress(0.0, ProgressStatus::Starting);

    auto start = std::chrono::steady_clock::now();

    std::vector<BookCost> ordered;
    ordered.reserve(items.size() - 1);
    for (size_t i = 1; i < items.size(); ++i) {
        int32_t est = _forge_engine.estimate_forge_cost(items[0], items[i], reg);
        ordered.push_back({i, est});
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const BookCost& a, const BookCost& b) { return a.est_cost < b.est_cost; });

    std::vector<EnchStep> compact_steps;
    int32_t step_index = 0;
    std::vector<Item> mutable_items = items;

    for (const auto& bc : ordered) {
        if (ctx.is_cancelled()) return;
        ctx.wait_if_paused();

        // Target already met — stop early
        if (meets_target(mutable_items[0], _target))
            break;

        auto& target_item = mutable_items[0];
        const auto& sacrifice = mutable_items[bc.index];

        if (!_forge_engine.is_forgeable(target_item, sacrifice))
            continue;

        Item before_target = target_item;
        Item before_sacrifice = sacrifice;
        int32_t cost = _forge_engine.forge_into(target_item, sacrifice, reg);
        ctx.incr_steps_forged();

        compact_steps.push_back({std::move(before_target), std::move(before_sacrifice), cost});

        {
            const auto& sc = input.search;
            if (sc.max_search_time.count() > 0) {
                auto elapsed = std::chrono::steady_clock::now() - start;
                if (elapsed > sc.max_search_time) break;
            }
        }

        ctx.report_progress((step_index + 1.0) / ordered.size(), ProgressStatus::ApplyingSacrifice);
        step_index++;

        // Check again after forge
        if (meets_target(mutable_items[0], _target))
            break;
    }

    bool goal_achieved = meets_target(mutable_items[0], _target);
    _diag.status = goal_achieved ? "Complete" : "CompleteNoSolution";
    _diag.flush(ctx);

    if (!goal_achieved) {
        ctx.report_progress(1.0, ProgressStatus::CompleteNoSolution);
        return;
    }

    ctx.report_compact_solution(std::move(compact_steps));
    ctx.report_progress(1.0, ProgressStatus::Complete);
}

bool GreedyAlgorithm::simulate(const AlgorithmInput& input) const noexcept {
    const auto& items = input.items;
    const auto& reg = input.ench_reg;
    const auto& target = input.target;
    if (items.empty()) return false;

    // Quick check: target already met?
    {
        bool met = true;
        for (const auto& t : target) {
            auto it = items[0].enchs.find(t.id);
            if (it == items[0].enchs.end() || it->level < t.level) { met = false; break; }
        }
        if (met) return true;
    }

    // Greedy pure-forge: copy items and try each sacrifice sequentially
    auto work = items;
    ForgeEngine engine(input.config);

    for (size_t i = 1; i < work.size(); ++i) {
        if (!engine.is_forgeable(work[0], work[i]))
            continue;

        engine.pure_forge_into(work[0], work[i], reg);

        bool met = true;
        for (const auto& t : target) {
            auto it = work[0].enchs.find(t.id);
            if (it == work[0].enchs.end() || it->level < t.level) { met = false; break; }
        }
        if (met) return true;
    }

    return false;
}

