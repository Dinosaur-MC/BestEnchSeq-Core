#include "IDAStarAlgorithm.h"
#include "../ExecutionContext.h"
#include "algorithm/components/Heuristic.h"
#include "algorithm/components/StateHash.h"
#include <algorithm>

using compact::Item;
using compact::EnchStep;
using compact::EnchReg;

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

void IDAStarAlgorithm::_dfs(std::vector<ItemID> ids, int32_t g,
                             int32_t& best_cost, ExecutionContext& ctx)
{
    ++_nodes_visited;

    if (_nodes_visited % 512 == 0) {
        if (ctx.is_cancelled()) return;
        ctx.wait_if_paused();
    }

    // Goal check BEFORE pruning — never prune a solution
    if (_meets_target(ids)) {
        if (g <= best_cost) {
            best_cost = g;
            _solution_path = _current_path;
        }
        return;
    }

    // Heuristic pruning
    int32_t h_val = Heuristic::compute(ids, _pool, *_ench_reg, _target,
                                        _book_mult, _h_buf, _h_dirty);
    if (g + h_val >= best_cost) {
        ++_nodes_pruned;
        return;
    }

    // TT: global best_g
    size_t h = StateHash::ids(ids, _pool);
    if (const int32_t* tt_g = _tt.lookup(h)) {
        if (*tt_g <= g) {
            ++_nodes_pruned;
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
            if (!_compact_forge.is_forgeable(_pool[ids[i]], _pool[ids[j]]))
                continue;
            int32_t est = _compact_forge.estimate_forge_cost(
                _pool[ids[i]], _pool[ids[j]], *_ench_reg);
            if (g + est <= best_cost)
                candidates.push_back({i, j, est});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.est_cost < b.est_cost;
              });

    for (const auto& cand : candidates) {
        if (g + cand.est_cost > best_cost) continue;

        ItemID old_base_id = ids[cand.i];
        ItemID old_sac_id  = ids[cand.j];
        Item forged = _pool[old_base_id];
        int32_t real_cost = _compact_forge.forge_into(forged, _pool[old_sac_id], *_ench_reg);
        int32_t child_g = g + real_cost;

        if (child_g > best_cost) continue;

        std::vector<ItemID> child_ids = ids;
        child_ids.erase(child_ids.begin() + static_cast<std::ptrdiff_t>(cand.j));
        size_t base_in_child = (cand.i > cand.j) ? cand.i - 1 : cand.i;

        ItemID new_base_id = _pool.add(std::move(forged));
        if (new_base_id == ItemPool::INVALID_ITEM_ID) continue;
        child_ids[base_in_child] = new_base_id;

        _current_path.push_back(IDALightStep{old_base_id, old_sac_id, real_cost});
        _dfs(std::move(child_ids), child_g, best_cost, ctx);
        _current_path.pop_back();

        if (ctx.is_cancelled()) return;
    }
}

void IDAStarAlgorithm::execute(
    const std::vector<Item>& items,
    const EnchReg& reg,
    const std::vector<compact::Ench>& target,
    ExecutionContext& ctx)
{
    ctx.report_progress(0.0, ProgressStatus::Starting);

    _pool.clear();
    _tt.clear();
    _current_path.clear();
    _solution_path.clear();
    _ench_reg = &reg;
    _target = target;
    _nodes_visited = 0;
    _nodes_pruned = 0;

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

    // Upper bound: greedy
    int32_t best_cost = INT32_MAX;
    if (items.size() > 1) {
        Item equip = items[0];
        int32_t greedy_cost = 0;
        for (size_t k = 1; k < items.size(); ++k) {
            if (_compact_forge.is_forgeable(equip, items[k]))
                greedy_cost += _compact_forge.forge_into(equip, items[k], reg);
        }
        if (greedy_cost > 0) best_cost = greedy_cost;
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
