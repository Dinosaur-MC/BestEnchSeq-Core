#include "AStarAlgorithm.h"
#include "../ExecutionContext.h"
#include <algorithm>
#include <chrono>
#include <queue>
#include <unordered_map>

using compact::Item;
using compact::EnchStep;
using compact::EnchReg;

// ─── Hash (local TU helper) ─────────────────────────────────────────────

namespace {

size_t hash_item_data(const Item& item) noexcept {
    size_t h = static_cast<size_t>(item.type)
             ^ (static_cast<size_t>(item.ppn) << 8)
             ^ (static_cast<size_t>(item.dur) << 16);
    h ^= item.enchs.hash() + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

} // anonymous namespace

// ─── ItemPool helpers ───────────────────────────────────────────────────

size_t AStarAlgorithm::_hash_ids(const std::vector<ItemID>& ids) const {
    size_t h = ids.size();
    for (auto id : ids)
        h ^= hash_item_data(_pool[id]) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

int32_t AStarAlgorithm::_heuristic(const std::vector<ItemID>& ids) const {
    int32_t h = 0;
    if (ids.empty()) return h;

    std::unordered_map<int16_t, int16_t> max_levels;
    for (auto id : ids) {
        for (const auto& e : _pool[id].enchs) {
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

bool AStarAlgorithm::_meets_target(ItemID equip_id) const {
    const auto& equip = _pool[equip_id];
    for (const auto& t : _target) {
        auto it = equip.enchs.find(t.id);
        if (it == equip.enchs.end() || it->level < t.level)
            return false;
    }
    return true;
}

// ─── Greedy bound (unchanged — operates on raw Items at startup) ────────

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

// ─── Execute ────────────────────────────────────────────────────────────

void AStarAlgorithm::execute(
    const std::vector<Item>& items,
    const EnchReg& reg,
    const std::vector<compact::Ench>& target,
    ExecutionContext& ctx)
{
    ctx.report_progress(0.0, ProgressStatus::Starting);
    auto t0 = std::chrono::steady_clock::now();

    // Reset state
    _pool.clear();
    _step_pool.clear();
    _open_heap.clear();
    _ench_reg = &reg;
    _target = target;
    _best_solution_cost = INT32_MAX;
    _diag = AStarDiagnostics{};

    // Seed ItemPool with initial items
    std::vector<ItemID> initial_ids;
    initial_ids.reserve(items.size());
    for (const auto& item : items) {
        ItemID id = _pool.add(Item(item));  // shallow copy
        initial_ids.push_back(id);
    }

    // Guard: empty input produces no solution
    if (initial_ids.empty()) {
        ctx.report_progress(1.0, ProgressStatus::CompleteNoSolution);
        return;
    }

    // Guard: single-item non-target — no books to forge
    if (initial_ids.size() == 1 && !_meets_target(initial_ids[0])) {
        ctx.report_progress(1.0, ProgressStatus::CompleteNoSolution);
        return;
    }

    // Greedy bound for pruning
    if (items.size() > 1) {
        _best_solution_cost = _greedy_bound(items, reg);
    }

    if (_meets_target(initial_ids[0])) {
        ctx.report_progress(1.0, ProgressStatus::GoalAlreadyMet);
        ctx.report_compact_solution({});
        return;
    }

    // Compute budget if not set (check SearchConfig for override first)
    if (_budget.max_explored == 0) {
        auto sc = ctx.get_search_config();
        if (sc.memory_mb > 0) {
            _budget = AStarMemoryBudget::from_memory_mb(sc.memory_mb,
                         static_cast<int32_t>(items.size()));
        } else {
            _budget = AStarMemoryBudget::auto_detect(
                static_cast<int32_t>(items.size()));
        }
    }

    // Pre-allocate
    _pool.set_max(_budget.max_items_pool);
    _pool.reserve(_budget.reserve_items_pool);
    _step_pool.reserve(_budget.reserve_step_pool);
    _open_heap.reserve(_budget.reserve_open_set);

    int32_t h0 = _heuristic(initial_ids);

    // Priority queue over backing heap
    std::priority_queue<
        PriorityEntry,
        std::vector<PriorityEntry>,
        std::greater<>
    > open_set(std::greater<>{}, std::move(_open_heap));

    open_set.push(PriorityEntry{
        SearchState{0, -1, std::move(initial_ids)}, h0
    });

    // best_g keyed by hash
    std::unordered_map<size_t, int32_t> best_g;
    best_g.reserve(static_cast<size_t>(_budget.max_explored));
    int64_t explored = 0;

    while (!open_set.empty() && !ctx.is_cancelled()) {
        ctx.wait_if_paused();

        SearchState current = std::move(
            const_cast<PriorityEntry&>(open_set.top())).state;
        open_set.pop();

        // best_g check
        size_t cur_h = _hash_ids(current.ids);
        auto bg_it = best_g.find(cur_h);
        if (bg_it != best_g.end() && bg_it->second < current.g)
            continue;

        // Goal check
        if (_meets_target(current.ids[0])) {
            if (current.g < _best_solution_cost)
                _best_solution_cost = current.g;

            // Rebuild steps from StepNode chain
            std::vector<EnchStep> steps;
            {
                std::vector<int32_t> indices;
                for (int32_t si = current.step_idx; si >= 0; si = _step_pool[si].prev)
                    indices.push_back(si);
                steps.reserve(indices.size());
                for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
                    const auto& sn = _step_pool[*it];
                    steps.push_back({_pool[sn.base_id], _pool[sn.sac_id], sn.cost});
                }
            }
            ctx.report_compact_solution(std::move(steps));
            ctx.report_progress(1.0, ProgressStatus::Complete);

            _diag.explored_count = explored;
            _diag.best_g_size = best_g.size();
            _diag.step_pool_used = _step_pool.size();
            _diag.step_pool_capacity = _step_pool.capacity();
            _diag.items_pool_size = _pool.size();
            _diag.items_pool_capacity = _pool.capacity();
            _diag.solution_cost = _best_solution_cost;
            _diag.wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            _diag.open_set_pending = open_set.size();
            _diag.estimated_peak_bytes =
                static_cast<int64_t>(_pool.capacity()) * static_cast<int64_t>(sizeof(Item))
              + static_cast<int64_t>(_step_pool.capacity()) * static_cast<int64_t>(sizeof(StepNode))
              + static_cast<int64_t>(_open_heap.capacity()) * static_cast<int64_t>(sizeof(PriorityEntry));
            _diag.status = "Complete";
            _diag.write();
            return;
        }

        explored++;
        if (explored >= _budget.max_explored) break;

        if (explored % 1000 == 0) {
            double progress = std::min(1.0 - 1.0 / (1.0 + explored * 0.0001), 0.99);
            ctx.report_progress(progress, ProgressStatus::Exploring);
        }

        // ─── Expand current state ────────────────────────────────────────
        const auto& cur_ids = current.ids;
        const size_t n = cur_ids.size();

        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                if (i == j) continue;

                if (!_compact_forge.is_forgeable(_pool[cur_ids[i]], _pool[cur_ids[j]]))
                    continue;

                // ── Phase A: Lightweight pre-pruning (zero Item copies) ──
                int32_t est = _compact_forge.estimate_forge_cost(
                    _pool[cur_ids[i]], _pool[cur_ids[j]], reg);
                int32_t child_est_g = current.g + est;
                if (_best_solution_cost != INT32_MAX && child_est_g > _best_solution_cost) {
                    ++_diag.pruned_by_cost;
                    continue;
                }

                // Build tentative child IDs (pre-forge, just erase sacrifice)
                std::vector<ItemID> child_ids = cur_ids;
                child_ids.erase(child_ids.begin() + static_cast<std::ptrdiff_t>(j));
                size_t base_in_child = (i > j) ? i - 1 : i;

                // Pool capacity check (avoid forging when no room)
                bool at_cap = (_step_pool.size() >= _budget.max_step_pool
                            || open_set.size() >= static_cast<size_t>(_budget.max_open_set));
                if (at_cap) {
                    ++_diag.pruned_by_caps;
                    continue;
                }

                // ── Phase B: Real forge (only survivors from Phase A) ────
                ItemID old_base_id = cur_ids[i];
                ItemID old_sac_id  = cur_ids[j];
                Item forged = _pool[old_base_id];
                int32_t real_cost = _compact_forge.forge_into(forged, _pool[old_sac_id], reg);
                int32_t child_g = current.g + real_cost;
                ++_diag.steps_forged;

                // Real cost may exceed estimate — recheck
                if (_best_solution_cost != INT32_MAX && child_g > _best_solution_cost)
                    continue;

                // Add forged Item to pool
                ItemID new_base_id = _pool.add(std::move(forged));
                if (new_base_id == INVALID_ITEM_ID) continue;  // pool full
                child_ids[base_in_child] = new_base_id;  // NOW we have real state

                // ── Phase C: heuristic + best_g + enqueue (real post-forge) ─
                int32_t child_f = child_g + _heuristic(child_ids);
                if (_best_solution_cost != INT32_MAX && child_f > _best_solution_cost) {
                    ++_diag.pruned_by_f;
                    continue;
                }

                size_t child_h = _hash_ids(child_ids);
                auto c_it = best_g.find(child_h);
                if (c_it != best_g.end() && c_it->second <= child_g) {
                    ++_diag.pruned_by_best_g;
                    continue;
                }
                best_g[child_h] = child_g;

                // Step node
                _step_pool.push_back({
                    current.step_idx,
                    old_base_id,
                    old_sac_id,
                    real_cost
                });
                int32_t step_idx = static_cast<int32_t>(_step_pool.size()) - 1;

                open_set.push(PriorityEntry{
                    SearchState{child_g, step_idx, std::move(child_ids)},
                    child_f
                });
            }
        }
    }

    // ─── Exit diagnostics ────────────────────────────────────────────────
    _diag.explored_count = explored;
    _diag.best_g_size = best_g.size();
    _diag.step_pool_used = _step_pool.size();
    _diag.step_pool_capacity = _step_pool.capacity();
    _diag.items_pool_size = _pool.size();
    _diag.items_pool_capacity = _pool.capacity();
    _diag.open_set_pending = open_set.size();
    _diag.solution_cost = _best_solution_cost;
    _diag.wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    _diag.estimated_peak_bytes =
        static_cast<int64_t>(_pool.capacity()) * static_cast<int64_t>(sizeof(Item))
      + static_cast<int64_t>(_step_pool.capacity()) * static_cast<int64_t>(sizeof(StepNode))
      + static_cast<int64_t>(_open_heap.capacity()) * static_cast<int64_t>(sizeof(PriorityEntry));
    if (ctx.is_cancelled()) {
        ctx.report_progress(1.0, ProgressStatus::Cancelled);
        _diag.status = "Cancelled";
    } else {
        ctx.report_progress(1.0, ProgressStatus::CompleteNoSolution);
        _diag.status = "CompleteNoSolution";
    }
    _diag.write();
}
