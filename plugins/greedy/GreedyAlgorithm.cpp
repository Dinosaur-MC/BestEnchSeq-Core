#include "GreedyAlgorithm.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/components/SearchUtils.h"
#include <chrono>
#include <algorithm>

namespace algorithm {

using namespace algorithm;

void GreedyAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    _forge_engine.set_config(input.f_config);
    _target.clear();
    for (const auto& e : input.target.enchs)
        _target.push_back(e);
    const auto& items = input.items;
    const auto& reg = input.ench_reg;
    ctx.report_progress(0, ProgressStatus::Starting);

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
            const auto& sc = input.s_config;
            if (sc.max_search_time.count() > 0) {
                auto elapsed = std::chrono::steady_clock::now() - start;
                if (elapsed > sc.max_search_time) break;
            }
        }

        ctx.report_progress(static_cast<uint8_t>((step_index + 1) * 100 / ordered.size()), ProgressStatus::ApplyingSacrifice);
        step_index++;

        // Check again after forge
        if (meets_target(mutable_items[0], _target))
            break;
    }

    bool goal_achieved = meets_target(mutable_items[0], _target);
    _diag.status = goal_achieved ? "Complete" : "CompleteNoSolution";
    ctx.set_exit_diagnostics(_diag);

    if (!goal_achieved) {
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
        return;
    }

    ctx.report_solution(compact_steps);
    ctx.report_progress(100, ProgressStatus::Complete);
}

bool GreedyAlgorithm::simulate(const AlgorithmInput& input) const noexcept {
    const auto& items = input.items;
    const auto& reg = input.ench_reg;
    const auto& target = input.target;
    if (items.empty()) return false;

    // Quick check: target already met?
    if (meets_target(items[0], target.enchs))
        return true;

    // Greedy pure-forge: copy items and try each sacrifice sequentially
    auto work = items;
    ForgeEngine engine(input.f_config);

    for (size_t i = 1; i < work.size(); ++i) {
        if (!engine.is_forgeable(work[0], work[i]))
            continue;

        engine.pure_forge_into(work[0], work[i], reg);

        if (meets_target(work[0], target.enchs))
            return true;
    }

    return false;
}

} // namespace algorithm
