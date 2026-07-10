#include "AStarAlgorithm.h"
#include "../ExecutionContext.h"
#include <algorithm>
#include <queue>
#include <unordered_map>
#include <vector>

using compact::Item;
using compact::EnchStep;
using compact::EnchReg;

// ─── Hash helper (local to TU) ─────────────────────────────────────────────

namespace {

size_t hash_items(const std::vector<Item>& items) noexcept {
    size_t h = items.size();
    for (const auto& item : items)
        h ^= std::hash<compact::Item>{}(item) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

} // anonymous namespace

// ─── Heuristic: admissible lower bound ─────────────────────────────────────

int32_t AStarAlgorithm::heuristic(const std::vector<Item>& items) const {
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
            int32_t bm = _compact_forge.book_multiplier(_ench_reg->get_multiplier(t.id));
            h += (t.level - have) * bm;
        }
    }
    return h;
}

// ─── Goal check ────────────────────────────────────────────────────────────

bool AStarAlgorithm::meets_target(const Item& equipment) const {
    for (const auto& t : _target) {
        auto it = equipment.enchs.find(t.id);
        if (it == equipment.enchs.end() || it->level < t.level)
            return false;
    }
    return true;
}

// ─── Greedy bound ──────────────────────────────────────────────────────────

int32_t AStarAlgorithm::_greedy_bound(
    const std::vector<Item>& items,
    const EnchReg& reg) const
{
    if (items.size() <= 1) return INT32_MAX;

    Item equip = items[0];
    std::vector<Item> books(items.begin() + 1, items.end());

    int32_t total_cost = 0;
    std::vector<std::pair<size_t, int32_t>> ordered;
    for (size_t i = 0; i < books.size(); ++i)
        ordered.emplace_back(i, _compact_forge.estimate_forge_cost(equip, books[i], reg));
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    for (const auto& [idx, _] : ordered) {
        if (!_compact_forge.is_forgeable(equip, books[idx]))
            continue;
        total_cost += _compact_forge.forge_into(equip, books[idx], reg);
    }

    for (const auto& t : _target) {
        auto it = equip.enchs.find(t.id);
        if (it == equip.enchs.end() || it->level < t.level)
            return INT32_MAX;
    }
    return total_cost;
}

// ─── A* execute ────────────────────────────────────────────────────────────

void AStarAlgorithm::execute(
    const std::vector<Item>& items,
    const EnchReg& reg,
    const std::vector<compact::Ench>& target,
    ExecutionContext& ctx)
{
    ctx.report_progress(0.0, ProgressStatus::Starting);
    _step_pool.clear();
    _ench_reg = &reg;
    _target = target;
    _best_solution_cost = _greedy_bound(items, reg);

    if (meets_target(items[0])) {
        ctx.report_progress(1.0, ProgressStatus::GoalAlreadyMet);
        ctx.report_compact_solution({});
        return;
    }

    int32_t h0 = heuristic(items);

    std::priority_queue<PriorityState,
                        std::vector<PriorityState>,
                        std::greater<>> open_set;

    auto init_items = std::make_shared<const std::vector<Item>>(items);
    SearchState init_state{init_items, 0, nullptr};
    open_set.push({std::move(init_state), h0});

    // best_g keyed by hash — tiny footprint
    std::unordered_map<size_t, int32_t> best_g;
    int64_t explored = 0;
    constexpr int64_t MAX_EXPLORED   = 5000000;
    constexpr size_t  MAX_OPEN_SET   = 2000000;
    constexpr size_t  MAX_STEP_POOL  = 4000000;

    while (!open_set.empty() && !ctx.is_cancelled()) {
        ctx.wait_if_paused();

        PriorityState current = std::move(
            const_cast<PriorityState&>(open_set.top()));
        open_set.pop();

        // best_g lookup by hash
        {
            size_t cur_h = hash_items(*current.state.items);
            auto bg_it = best_g.find(cur_h);
            if (bg_it != best_g.end() && bg_it->second < current.state.g)
                continue;
        }

        explored++;
        if (explored >= MAX_EXPLORED) break;

        if (explored % 1000 == 0) {
            double progress = std::min(1.0 - 1.0 / (1.0 + explored * 0.0001), 0.99);
            ctx.report_progress(progress, ProgressStatus::Exploring);
        }

        const auto& cur_items = *current.state.items;

        if (meets_target(cur_items[0])) {
            if (current.state.g < _best_solution_cost)
                _best_solution_cost = current.state.g;

            std::vector<EnchStep> steps;
            {
                std::vector<const CompactStepNode*> nodes;
                for (auto* s = current.state.steps_tail; s; s = s->prev)
                    nodes.push_back(s);
                steps.reserve(nodes.size());
                for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
                    steps.push_back((*it)->step);
            }
            ctx.report_compact_solution(std::move(steps));
            ctx.report_progress(1.0, ProgressStatus::Complete);
            return;
        }

        const size_t n = cur_items.size();
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                if (i == j) continue;

                if (!_compact_forge.is_forgeable(cur_items[i], cur_items[j]))
                    continue;

                Item base_item = cur_items[i];
                Item sac_item  = cur_items[j];

                // Create a mutable copy for the child state
                auto child_ptr = std::make_shared<std::vector<Item>>(cur_items);
                auto& child_vec = *child_ptr;

                auto [result, step_cost] = _compact_forge.forge(
                    child_vec[i], child_vec[j], *_ench_reg);

                child_vec[i] = result;
                child_vec.erase(child_vec.begin() + j);

                int32_t child_g = current.state.g + step_cost;

                if (_best_solution_cost != INT32_MAX && child_g > _best_solution_cost)
                    continue;

                size_t child_h = hash_items(child_vec);

                // Memory caps: if step pool or open_set are full, stop
                // adding new work and drain what's queued.
                if (_step_pool.size() >= MAX_STEP_POOL
                    || open_set.size() >= MAX_OPEN_SET)
                    continue;

                const CompactStepNode* step_node = alloc_step(
                    current.state.steps_tail,
                    EnchStep{std::move(base_item), std::move(sac_item), step_cost});

                SearchState child_state{std::move(child_ptr), child_g, step_node};

                {
                    auto c_it = best_g.find(child_h);
                    if (c_it != best_g.end() && c_it->second <= child_g)
                        continue;
                }

                int32_t child_f = child_g + heuristic(*child_state.items);

                if (_best_solution_cost != INT32_MAX && child_f > _best_solution_cost)
                    continue;

                best_g[child_h] = child_g;
                open_set.push({std::move(child_state), child_f});
            }
        }
    }

    if (ctx.is_cancelled())
        ctx.report_progress(1.0, ProgressStatus::Cancelled);
    else
        ctx.report_progress(1.0, ProgressStatus::CompleteNoSolution);
}
