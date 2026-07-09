#include "CompactAStarAlgorithm.h"
#include "utils/CompactAdapter.hpp"
#include "utils/ExpCalculator.hpp"
#include <queue>
#include <unordered_map>

// ─── Heuristic: admissible lower bound on compact items ────────────────────

int32_t CompactAStarAlgorithm::heuristic(const std::vector<compact::Item>& items) const {
    int32_t h = 0;
    const auto& equipment = items[0];

    for (const auto& t : _target) {
        auto it = std::find_if(equipment.enchs.begin(), equipment.enchs.end(),
            [&](const compact::Ench& e) { return e.id == t.id; });
        int32_t have = (it == equipment.enchs.end()) ? 0 : it->level;
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
        auto it = std::find_if(equipment.enchs.begin(), equipment.enchs.end(),
            [&](const compact::Ench& e) { return e.id == t.id; });
        if (it == equipment.enchs.end() || it->level < t.level)
            return false;
    }
    return true;
}

// ─── A* execute ────────────────────────────────────────────────────────────

void CompactAStarAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    ctx.report_progress(0.0, ProgressStatus::Starting);
    _step_pool.clear();

    //── Boundary: prepare compact data ────────────────────────────────────
    auto& ench_reg = compact::EnchReg::get_instance();
    ench_reg.init(EnchantmentRegistry::get_instance(), *input.target_item.equipment);
    _ench_reg = &ench_reg;

    auto ci = compact::prepare(input, ench_reg);
    auto& items = ci.items;

    // Extract target enchantments in compact form (once, at boundary)
    _target.clear();
    _target.reserve(input.target_item.enchantments.size());
    for (const auto& e : input.target_item.enchantments)
        _target.push_back({static_cast<int16_t>(e.id), static_cast<int16_t>(e.level)});

    // Quick check: goal already met?
    if (meets_target(items[0])) {
        ctx.report_progress(1.0, ProgressStatus::GoalAlreadyMet);
        ctx.report_solution_found({});
        return;
    }

    // 1. Initial heuristic
    int32_t h0 = heuristic(items);

    // 2. Open set: min-heap ordered by f = g + h
    std::priority_queue<PriorityState,
                        std::vector<PriorityState>,
                        std::greater<>> open_set;

    SearchState init_state{std::move(items), 0, nullptr};
    open_set.push({std::move(init_state), h0});

    // 3. Closed set: state -> best known g-cost
    std::unordered_map<SearchState, int32_t, StateHash, StateEqual> best_g;

    int64_t explored = 0;

    // 4. Main A* loop
    while (!open_set.empty() && !ctx.is_cancelled()) {
        ctx.wait_if_paused();

        PriorityState current = std::move(
            const_cast<PriorityState&>(open_set.top()));
        open_set.pop();

        // Skip stale entries
        auto bg_it = best_g.find(current.state);
        if (bg_it != best_g.end() && bg_it->second < current.state.g)
            continue;

        explored++;

        if (explored % 1000 == 0) {
            double progress = std::min(1.0 - 1.0 / (1.0 + explored * 0.0001), 0.99);
            ctx.report_progress(progress, ProgressStatus::Exploring);
        }

        // Goal check (compact only)
        if (meets_target(current.state.items[0])) {
            //── Boundary: convert compact steps to domain at output ──────
            EnchStepList steps;
            {
                std::vector<const CompactStepNode*> nodes;
                for (auto* s = current.state.steps_tail; s; s = s->prev)
                    nodes.push_back(s);
                steps.reserve(nodes.size());
                for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
                    steps.push_back({
                        compact::to_domain((*it)->base, ci.equipment),
                        compact::to_domain((*it)->sacrifice, ci.equipment),
                        (*it)->cost,
                        ExpCalculator::level_to_exp((*it)->cost)
                    });
                }
            }
            ctx.report_solution_found(steps);
            ctx.report_progress(1.0, ProgressStatus::Complete);
            return;
        }

        // Expand: try every forgeable pair (i, j)
        const size_t n = current.state.items.size();
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                if (i == j) continue;

                if (!compact::CompactForgeEngine::is_forgeable(
                        current.state.items[i], current.state.items[j]))
                    continue;

                // Save originals for step recording (compact copies)
                compact::Item base_item = current.state.items[i];
                compact::Item sac_item  = current.state.items[j];

                // Build child items by copying parent
                std::vector<compact::Item> child_items = current.state.items;

                // Forge (compact only, no domain types)
                auto [result, step_cost] = _compact_forge.forge(
                    child_items[i], child_items[j], *_ench_reg);

                child_items[i] = result;
                child_items.erase(child_items.begin() + j);

                int32_t child_g = current.state.g + step_cost;

                // Record step (compact — no conversion)
                const CompactStepNode* step_node = alloc_step(
                    current.state.steps_tail,
                    std::move(base_item), std::move(sac_item), step_cost);

                SearchState child_state{
                    std::move(child_items),
                    child_g,
                    step_node
                };

                // Closed set check
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
