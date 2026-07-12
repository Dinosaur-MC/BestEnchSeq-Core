#include "GreedyAlgorithm.h"
#include "../ExecutionContext.h"
#include <chrono>
#include <algorithm>

using compact::Item;
using compact::EnchStep;
using compact::EnchReg;

void GreedyAlgorithm::execute(
    const std::vector<Item>& items,
    const EnchReg& reg,
    const std::vector<compact::Ench>& target,
    ExecutionContext& ctx)
{
    _target = target;
    ctx.report_progress(0.0, ProgressStatus::Starting);

    auto _start = std::chrono::steady_clock::now();

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
        if (_meets_target(mutable_items[0]))
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
            auto cfg = ctx.get_search_config();
            if (cfg.max_search_time.count() > 0) {
                auto elapsed = std::chrono::steady_clock::now() - _start;
                if (elapsed > cfg.max_search_time) break;
            }
        }

        ctx.report_progress((step_index + 1.0) / ordered.size(), ProgressStatus::ApplyingSacrifice);
        step_index++;

        // Check again after forge
        if (_meets_target(mutable_items[0]))
            break;
    }

    _diag.label = "greedy";
    _diag.steps_forged = compact_steps.size();
    _diag.status = compact_steps.empty() ? "GoalAlreadyMet" : "Complete";
    _diag.write();

    ctx.report_compact_solution(std::move(compact_steps));
    ctx.report_progress(1.0, compact_steps.empty()
        ? ProgressStatus::GoalAlreadyMet
        : ProgressStatus::Complete);
}

bool GreedyAlgorithm::_meets_target(const compact::Item& equipment) const {
    for (const auto& t : _target) {
        auto it = equipment.enchs.find(t.id);
        if (it == equipment.enchs.end() || it->level < t.level)
            return false;
    }
    return true;
}
