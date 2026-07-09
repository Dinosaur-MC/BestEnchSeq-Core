#include "CompactAStarAlgorithm.h"
#include "../ExecutionContext.h"
#include "utils/CompactForgeUtils.hpp"
#include <queue>
#include <unordered_map>

// ─── Heuristic: admissible lower bound on compact items ────────────────────

int32_t CompactAStarAlgorithm::heuristic(const std::vector<compact::Item>& items) const {
    int32_t h = 0;
    if (items.empty()) return h;

    std::unordered_map<int16_t, int16_t> max_levels;
    for (const auto& item : items) {
        for (const auto& e : item.enchs) {
            auto it = max_levels.find(e.id);
            if (it == max_levels.end())
                max_levels[e.id] = e.level;
            else if (e.level > it->second)
                it->second = e.level;
        }
    }

    for (const auto& t : _target) {
        auto it = max_levels.find(t.id);
        int16_t have = (it == max_levels.end()) ? 0 : it->second;
        if (have < t.level) {
            int32_t bm = compact::book_multiplier(_ench_reg->get_multiplier(t.id));
            h += (t.level - have) * bm;
        }
    }
    return h;
}

// ─── Goal check on compact items ──────────────────────────────────────────

bool CompactAStarAlgorithm::meets_target(const compact::Item& equipment) const {
    for (const auto& t : _target) {
        auto it = equipment.enchs.find(t.id);
        if (it == equipment.enchs.end() || it->level < t.level)
            return false;
    }
    return true;
}

// ─── A* execute (compact-only) ────────────────────────────────────────────

void CompactAStarAlgorithm::execute(
    const std::vector<compact::Item>& items,
    const compact::EnchReg& reg,
    const std::vector<compact::Ench>& target,
    ExecutionContext& ctx)
{
    ctx.report_progress(0.0, ProgressStatus::Starting);
    _step_pool.clear();
    _ench_reg = &reg;
    _target = target;

    // Quick check: goal already met?
    if (meets_target(items[0])) {
        ctx.report_progress(1.0, ProgressStatus::GoalAlreadyMet);
        ctx.report_compact_solution({});
        return;
    }

    int32_t h0 = heuristic(items);

    std::priority_queue<PriorityState,
                        std::vector<PriorityState>,
                        std::greater<>> open_set;

    SearchState init_state{items, 0, nullptr};
    open_set.push({std::move(init_state), h0});

    std::unordered_map<SearchState, int32_t, StateHash, StateEqual> best_g;

    int64_t explored = 0;

    while (!open_set.empty() && !ctx.is_cancelled()) {
        ctx.wait_if_paused();

        PriorityState current = std::move(
            const_cast<PriorityState&>(open_set.top()));
        open_set.pop();

        auto bg_it = best_g.find(current.state);
        if (bg_it != best_g.end() && bg_it->second < current.state.g)
            continue;

        explored++;

        if (explored % 1000 == 0) {
            double progress = std::min(1.0 - 1.0 / (1.0 + explored * 0.0001), 0.99);
            ctx.report_progress(progress, ProgressStatus::Exploring);
        }

        if (meets_target(current.state.items[0])) {
            // Flatten step chain into compact step vector
            std::vector<compact::EnchStep> steps;
            {
                std::vector<const CompactStepNode*> nodes;
                for (auto* s = current.state.steps_tail; s; s = s->prev)
                    nodes.push_back(s);
                steps.reserve(nodes.size());
                for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
                    steps.push_back((*it)->step);
            }
            ctx.report_compact_solution(steps);
            ctx.report_progress(1.0, ProgressStatus::Complete);
            return;
        }

        const size_t n = current.state.items.size();
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                if (i == j) continue;

                if (!compact::CompactForgeEngine::is_forgeable(
                        current.state.items[i], current.state.items[j]))
                    continue;

                compact::Item base_item = current.state.items[i];
                compact::Item sac_item  = current.state.items[j];

                std::vector<compact::Item> child_items = current.state.items;

                auto [result, step_cost] = _compact_forge.forge(
                    child_items[i], child_items[j], *_ench_reg);

                child_items[i] = result;
                child_items.erase(child_items.begin() + j);

                int32_t child_g = current.state.g + step_cost;

                const CompactStepNode* step_node = alloc_step(
                    current.state.steps_tail,
                    compact::EnchStep{
                        std::move(base_item),
                        std::move(sac_item),
                        step_cost
                    });

                SearchState child_state{
                    std::move(child_items),
                    child_g,
                    step_node
                };

                auto c_it = best_g.find(child_state);
                if (c_it != best_g.end() && c_it->second <= child_g)
                    continue;

                int32_t child_h = heuristic(child_state.items);
                int32_t child_f = child_g + child_h;

                best_g[child_state] = child_g;
                open_set.push({std::move(child_state), child_f});
            }
        }
    }

    if (ctx.is_cancelled()) {
        ctx.report_progress(1.0, ProgressStatus::Cancelled);
    } else {
        ctx.report_progress(1.0, ProgressStatus::CompleteNoSolution);
    }
}
