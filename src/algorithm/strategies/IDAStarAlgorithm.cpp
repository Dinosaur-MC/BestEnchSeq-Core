#include "IDAStarAlgorithm.h"
#include "../ExecutionContext.h"
#include "../Utils.h"
#include "algorithm/components/Heuristic.h"
#include "algorithm/components/HeuristicBasic.h"
#include "algorithm/components/StateHash.h"
#include <algorithm>

using compact::Item;
using compact::EnchStep;

bool IDAStarAlgorithm::_meets_target(const std::vector<ItemID>& ids) const {
    if (ids.empty()) return false;
    const auto& equip = _pool[ids[0]];
    for (const auto& t : _target) {
        auto it = equip.enchs.find(t.id);
        if (it == equip.enchs.end() || it->level < t.level)
            return false;
    }
    return true;
}

void IDAStarAlgorithm::_dfs(std::vector<ItemID>& ids, int32_t g,
                             int32_t& best_cost, ExecutionContext& ctx)
{
    ctx.incr_nodes_visited();
    ++_nodes_visited;

    if (_nodes_visited % 512 == 0) {
        if (ctx.is_cancelled()) return;
        ctx.wait_if_paused();

        if (_max_search_time.count() > 0) {
            auto elapsed = std::chrono::steady_clock::now() - _start_time;
            if (elapsed > _max_search_time) return;
        }
    }

    // Goal check BEFORE pruning — never prune a solution
    if (_meets_target(ids)) {
        ++_solutions_found;
        if (g <= best_cost) {
            best_cost = g;
            _solution_path = _current_path;
        }
        {
            if (_max_solutions > 0 && _solutions_found >= _max_solutions)
                ctx.cancel();
        }
        return;
    }

    // Heuristic pruning
    int32_t h_val = Heuristic::compute(ids, _pool, *_ench_reg, _target,
                                        _h_buf, _h_dirty);
    if (g + h_val >= best_cost) {
        ctx.incr_nodes_pruned();
        return;
    }

    // TT: global best_g
    size_t h = StateHash::ids(ids, _pool);
    if (const int32_t* tt_g = _tt.lookup(h)) {
        if (*tt_g <= g) {
            ctx.incr_nodes_pruned();
            return;
        }
    }
    _tt.store(h, g);

    // Collect forgeable pairs sorted by estimated cost
    struct Candidate { size_t i, j; int32_t est_cost; };
    const size_t n = ids.size();
    std::vector<Candidate> candidates;
    candidates.reserve(n * (n - 1));

    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            if (!_forge_engine.is_forgeable(_pool[ids[i]], _pool[ids[j]]))
                continue;
            int32_t est = _forge_engine.estimate_forge_cost(
                _pool[ids[i]], _pool[ids[j]], *_ench_reg);
            if (g + est <= best_cost)
                candidates.push_back({i, j, est});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.est_cost < b.est_cost;
              });

    // Reusable child buffer — allocated once per stack frame.
    // clear() retains capacity, push_back reuses the storage across all
    // candidate iterations, eliminating ~70M heap (malloc+free) pairs.
    std::vector<ItemID> child_buf;
    child_buf.reserve(n > 0 ? n - 1 : 0);

    for (const auto& cand : candidates) {
        if (g + cand.est_cost > best_cost) continue;

        ItemID old_base_id = ids[cand.i];
        ItemID old_sac_id  = ids[cand.j];
        Item forged = _pool[old_base_id];
        int32_t real_cost = _forge_engine.forge_into(forged, _pool[old_sac_id], *_ench_reg);
        int32_t child_g = g + real_cost;
        ctx.incr_steps_forged();

        if (child_g > best_cost) continue;

        // Build child state: selective copy — avoids the copy+erase pattern
        // that allocated a new heap buffer per candidate.
        child_buf.clear();
        for (size_t k = 0; k < cand.j; ++k)
            child_buf.push_back(ids[k]);
        for (size_t k = cand.j + 1; k < n; ++k)
            child_buf.push_back(ids[k]);
        size_t base_in_child = (cand.i > cand.j) ? cand.i - 1 : cand.i;

        ItemID new_base_id = _pool.add(std::move(forged));
        if (new_base_id == ItemPool::INVALID_ITEM_ID) continue;
        child_buf[base_in_child] = new_base_id;

        _current_path.push_back(IDALightStep{old_base_id, old_sac_id, real_cost});
        _dfs(child_buf, child_g, best_cost, ctx);
        _current_path.pop_back();

        if (ctx.is_cancelled()) return;
    }
}

// ─── Limited DFS bound ────────────────────────────────────────────────

int32_t IDAStarAlgorithm::_dfs_bound(
    std::vector<compact::Item> items,
    int32_t g,
    int32_t best_cost,
    int64_t& node_limit) const
{
    if (node_limit <= 0)
        return best_cost;
    --node_limit;

    if (meets_target(items[0], _target))
        return (g < best_cost) ? g : best_cost;

    int32_t h = HeuristicBasic::compute(items, *_ench_reg, _target,
                                         _h_buf, _h_dirty);
    if (g + h >= best_cost)
        return best_cost;

    const size_t n = items.size();
    if (n < 2) return best_cost;

    struct Candidate { size_t i, j; int32_t est; };
    std::vector<Candidate> candidates;
    candidates.reserve(n * (n - 1));

    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            if (!_forge_engine.is_forgeable(items[i], items[j]))
                continue;
            int32_t est = _forge_engine.estimate_forge_cost(
                items[i], items[j], *_ench_reg);
            if (g + est < best_cost)
                candidates.push_back({i, j, est});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  // Equipment-base (i==0) first — builds complete solutions fast
                  if ((a.i == 0) != (b.i == 0))
                      return a.i == 0;
                  return a.est < b.est;
              });

    for (const auto& cand : candidates) {
        compact::Item forged = items[cand.i];
        int32_t real_cost = _forge_engine.forge_into(
            forged, items[cand.j], *_ench_reg);
        int32_t child_g = g + real_cost;
        if (child_g >= best_cost)
            continue;

        std::vector<compact::Item> child = items;
        child.erase(child.begin() + static_cast<std::ptrdiff_t>(cand.j));
        size_t base_in_child = (cand.i > cand.j) ? cand.i - 1 : cand.i;
        child[base_in_child] = std::move(forged);

        best_cost = _dfs_bound(std::move(child), child_g,
                               best_cost, node_limit);
        if (node_limit <= 0)
            return best_cost;
    }

    return best_cost;
}

void IDAStarAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    _forge_engine.set_config(input.config);
    const auto& items = input.items;
    const auto& reg = input.ench_reg;
    const auto& target = input.target;
    ctx.report_progress(0.0, ProgressStatus::Starting);

    _pool.clear();
    // Pre-allocate pool capacity based on problem size (factorial estimate).
    // Avoids ~O(log n) reallocations of the item vector + dedup hash table
    // during search.  At most 500k — well below the 10M default max.
    {
        size_t est = 64;
        if (items.size() > 1) {
            size_t f = 1;
            for (size_t k = 2; k <= items.size() && f <= (1u << 20); ++k) f *= k;
            est = std::max<size_t>(64, f);
        }
        _pool.reserve(std::min(est, size_t{500'000}));
    }
    _tt.clear();
    _current_path.clear();
    _solution_path.clear();
    _ench_reg = &reg;
    _target = target;
    _nodes_visited = 0;
    _solutions_found = 0;
    _start_time = std::chrono::steady_clock::now();

    // Cache config from AlgorithmInput
    _max_solutions = input.search.max_solutions;
    _max_search_time = input.search.max_search_time;

    std::vector<ItemID> initial_ids;
    initial_ids.reserve(items.size());
    for (const auto& item : items)
        initial_ids.push_back(_pool.add(Item(item)));

    if (initial_ids.empty() || (initial_ids.size() == 1 && !_meets_target(initial_ids))) {
        ctx.report_progress(1.0, ProgressStatus::CompleteNoSolution);
        return;
    }
    if (_meets_target(initial_ids)) {
        ctx.report_progress(1.0, ProgressStatus::GoalAlreadyMet);
        ctx.report_compact_solution({});
        return;
    }

    // Upper bound: greedy + limited DFS
    int32_t best_cost = INT32_MAX;
    if (items.size() > 1) {
        Item equip = items[0];
        int32_t greedy_cost = 0;
        for (size_t k = 1; k < items.size(); ++k) {
            if (_forge_engine.is_forgeable(equip, items[k]))
                greedy_cost += _forge_engine.forge_into(equip, items[k], reg);
        }
        if (greedy_cost > 0) best_cost = greedy_cost;

        int64_t node_limit = 50'000;
        int32_t dfs_cost = _dfs_bound(
            std::vector<compact::Item>(items.begin(), items.end()),
            0, best_cost, node_limit);
        if (dfs_cost < best_cost)
            best_cost = dfs_cost;
    }

    // Exhaustive DFS branch-and-bound
    _dfs(initial_ids, 0, best_cost, ctx);

    if (best_cost < INT32_MAX && !_solution_path.empty()) {
        std::vector<EnchStep> steps;
        steps.reserve(_solution_path.size());
        for (const auto& s : _solution_path)
            steps.push_back({_pool[s.base_id], _pool[s.sac_id], s.cost});

        ctx.report_compact_solution(std::move(steps));
        ctx.report_progress(1.0, ProgressStatus::Complete);
    } else {
        ctx.report_progress(1.0,
            ctx.is_cancelled() ? ProgressStatus::Cancelled
                               : ProgressStatus::CompleteNoSolution);
    }

}
