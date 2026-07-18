#include "algorithm/strategies/astar/AStarAlgorithm.h"
#include "algorithm/strategies/astar/AStarStateSerializer.h"
#include "algorithm/ExecutionContext.h"
#include "algorithm/components/SearchUtils.h"
#include "algorithm/components/StateHash.h"
#include "utils/FlatHashMap.hpp"
#include <algorithm>
#include <chrono>
#include <memory>
#include <queue>

using namespace compact;

// ─── Constructor ────────────────────────────────────────────────────────

AStarAlgorithm::AStarAlgorithm(ForgeConfig cfg)
    : _forge_engine(std::move(cfg))
    , _serializer(std::make_unique<AStarStateSerializer>())
{}

// ─── ItemPool helpers ───────────────────────────────────────────────────

size_t AStarAlgorithm::_hash_ids(const std::vector<ItemID>& ids) const {
    return StateHash::ids(ids, _pool);
}

int32_t AStarAlgorithm::_heuristic(const std::vector<ItemID>& ids) const {
    int32_t h = 0;
    if (ids.empty()) return h;

    if (_h_buf.size() < _ench_reg->size())
        _h_buf.assign(_ench_reg->size(), 0);
    _h_dirty.clear();

    for (auto id : ids) {
        for (const auto& e : _pool[id].enchs) {
            if (e.level > _h_buf[e.id]) {
                if (_h_buf[e.id] == 0)
                    _h_dirty.push_back(e.id);
                _h_buf[e.id] = e.level;
            }
        }
    }

    for (const auto& t : _target) {
        int16_t have = _h_buf[t.id];
        if (have < t.level) {
            int32_t bm = (*_ench_reg)[t.id].mul_b;
            h += (t.level - have) * bm;
        }
    }

    for (auto id : _h_dirty)
        _h_buf[id] = 0;

    return h;
}

bool AStarAlgorithm::_meets_target(ItemID equip_id) const {
    return meets_target(equip_id, _pool, _target);
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
        ordered.emplace_back(i, _forge_engine.estimate_forge_cost(equip, books[i], reg));
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    for (const auto& [idx, _] : ordered) {
        if (!_forge_engine.is_forgeable(equip, books[idx]))
            continue;
        total_cost += _forge_engine.forge_into(equip, books[idx], reg);
    }

    for (const auto& t : _target) {
        auto it = equip.enchs.find(t.id);
        if (it == equip.enchs.end() || it->level < t.level)
            return INT32_MAX;
    }
    return total_cost;
}

// ─── Delta heuristic helper ───────────────────────────────────────────

int32_t AStarAlgorithm::_delta_h(int32_t parent_h,
                                  const Item& forged,
                                  const Item& sacrifice) const
{
    int32_t h = parent_h;

    // Gains: target enchants that forged improved vs parent's global max
    for (const auto& e : forged.enchs) {
        if (e.id < 0) continue;
        int16_t target_level = _target_level_map[e.id];
        if (target_level == 0) continue;
        int16_t old_max = _h_max[e.id];
        if (e.level > old_max && old_max < target_level) {
            int16_t gain = (std::min)(e.level, target_level) - old_max;
            h -= static_cast<int32_t>(gain) * (*_ench_reg)[e.id].mul_b;
        }
    }

    // Losses from conflicts (sacrifice enchants not transferred):
    // Skipped — conflicts with target enchants are rare in practice.
    // Omitting this is admissible-safe: h never overestimates remaining cost.
    (void)sacrifice;

    return h;
}

// ─── Execute ────────────────────────────────────────────────────────────

void AStarAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    _forge_engine.set_config(input.config);
    const auto& items = input.items;
    const auto& reg = input.ench_reg;
    const auto& target = input.target;
    // ─── Restore dispatch ───────────────────────────────────────────────
    if (_state_restored) {
        _state_restored = false;
        if (!_deserialize_ok) {
            // Deserialization was incomplete — don't run
            ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
            return;
        }
        _restore_and_execute(_restored_input, ctx);
        return;
    }

    ctx.report_progress(0, ProgressStatus::Starting);
    auto t0 = std::chrono::steady_clock::now();

    // Reset state
    _pool.clear();
    _step_pool.clear();
    _open_heap.clear();
    _ench_reg = &input.ench_reg;
    _target = target;
    _target_level_map.assign(_ench_reg->size(), 0);
    for (const auto& t : _target)
        _target_level_map[t.id] = t.level;
    _best_solution_cost = INT32_MAX;
    _diag = AStarDiagnostics{};

    // Cache config from AlgorithmInput
    _max_solutions = input.search.max_solutions;
    _max_search_time = input.search.max_search_time;
    _budget = AStarMemoryBudget::from_memory_mb(
        input.search.memory_mb > 0 ? input.search.memory_mb : 2048,
        static_cast<int32_t>(items.size()));

    // Seed ItemPool with initial items
    std::vector<ItemID> initial_ids;
    initial_ids.reserve(items.size());
    for (const auto& item : items) {
        ItemID id = _pool.add(Item(item));  // shallow copy
        initial_ids.push_back(id);
    }

    // Guard: empty input produces no solution
    if (initial_ids.empty()) {
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
        return;
    }

    // Guard: single-item non-target — no books to forge
    if (initial_ids.size() == 1 && !_meets_target(initial_ids[0])) {
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
        return;
    }

    // Upper bound: greedy + limited DFS
    if (items.size() > 1) {
        _best_solution_cost = _greedy_bound(items, reg);

        // External warm-start bound (hamming, dfs chain) is typically already
        // tighter than our internal dfs_bound — skip to save ~25M Ir.
        if (input.initial_bound < _best_solution_cost) {
            _best_solution_cost = input.initial_bound;
        } else {
            int64_t node_limit = 50'000;
            int32_t dfs_cost = search_utils::dfs_bound(
                std::vector<Item>(items.begin(), items.end()),
                0, _best_solution_cost, node_limit,
                _forge_engine, *_ench_reg, _target,
                _h_buf, _h_dirty);
            if (dfs_cost < _best_solution_cost)
                _best_solution_cost = dfs_cost;
        }
    }

    // Warm-start bound (fallback: already checked above, but also covers items.size() <= 1)
    if (input.initial_bound < _best_solution_cost)
        _best_solution_cost = input.initial_bound;
    _diag.initial_bound = _best_solution_cost;

    if (_meets_target(initial_ids[0])) {
        ctx.report_progress(100, ProgressStatus::GoalAlreadyMet);
        ctx.report_solution({});
        return;
    }

    // Pre-allocate (task-size-aware — factorial estimate, capped at budget max)
    _pool.set_max(_budget.max_items_pool);
    {
        _state_est = 64;
        if (items.size() > 1) {
            size_t f = 1;
            for (size_t k = 2; k <= items.size() && f <= (1u << 20); ++k) f *= k;
            _state_est = std::max<size_t>(64, f);
        }
        _pool.reserve(std::min(_state_est, static_cast<size_t>(_budget.max_items_pool)));
        _step_pool.reserve(std::min(_state_est, _budget.max_step_pool));
        // Open set is typically smaller than explored set
        _open_heap.reserve(std::min(_state_est / 2 + 64, _budget.max_open_set));
    }

    int32_t h0 = _heuristic(initial_ids);
    size_t initial_hash = _hash_ids(initial_ids);
    size_t open_heap_cap = _open_heap.capacity();

    // Priority queue over backing heap
    std::priority_queue<
        PriorityEntry,
        std::vector<PriorityEntry>,
        std::greater<>
    > open_set(std::greater<>{}, std::move(_open_heap));

    open_set.push(PriorityEntry{
        SearchState{0, h0, initial_hash, -1, std::move(initial_ids)}, h0
    });

    // _best_g keyed by hash — open-addressing flat map (contiguous, cache-friendly).
    // Initial capacity estimated from problem size to avoid over-allocation
    // AND unnecessary rehashes.  Auto-grow in FlatHashMap handles overflow.
    {
        // Upper bound on unique states for N items: roughly N!
        size_t estimated = 64;
        if (items.size() > 1) {
            size_t f = 1;
            for (size_t k = 2; k <= items.size() && f <= (1u << 20); ++k)
                f *= k;
            estimated = std::max<size_t>(64, f);
        }
        _best_g.clear();
        _best_g.reserve(estimated);
    }
    _explored = 0;

    while (!open_set.empty() && !ctx.is_cancelled()) {
        ctx.wait_if_paused();

        // const_cast + move from top() is valid here because top() returns
        // a const& to an element that is immediately popped.  All major
        // stdlib implementations accept this pattern in practice.
        SearchState current = std::move(
            const_cast<PriorityEntry&>(open_set.top())).state;
        open_set.pop();

        // best_g check
        size_t cur_h = current.hash;
        if (int32_t* bg = _best_g.find(cur_h)) {
            if (*bg < current.g)
                continue;
        }

        // Goal check
        if (_meets_target(current.ids[0])) {
            ++_solutions_found;
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
            ctx.report_solution(steps);
            ctx.report_progress(100, ProgressStatus::Complete);

            _diag.explored_count = _explored;
            _diag.best_g_entries = _best_g.size();
            _diag.final_bound = _best_solution_cost;
            _diag.step_pool_used = _step_pool.size();
            _diag.step_pool_capacity = _step_pool.capacity();
            _diag.items_pool_used = _pool.size();
            _diag.items_pool_capacity = _pool.capacity();
            _diag.solution_cost = _best_solution_cost;
            _diag.open_set_pending = open_set.size();
            _diag.estimated_peak_bytes =
                static_cast<int64_t>(_pool.capacity()) * static_cast<int64_t>(sizeof(Item))
              + static_cast<int64_t>(_step_pool.capacity()) * static_cast<int64_t>(sizeof(StepNode))
              + static_cast<int64_t>(open_heap_cap) * static_cast<int64_t>(sizeof(PriorityEntry));
            _diag.status = "Complete";
            ctx.set_exit_diagnostics(_diag);
            return;
        }

        _explored++;
        ctx.incr_nodes_visited();
        if (_explored >= _budget.max_explored) break;

        if (_explored % 1024 == 0) {
            if (_max_search_time.count() > 0) {
                auto elapsed = std::chrono::steady_clock::now() - t0;
                if (elapsed > _max_search_time) break;
            }
            if (_max_solutions > 0 && _solutions_found >= _max_solutions) break;
        }

        if ((_explored & 0x3F) == 0) {  // every 64 states
            uint8_t progress;
            if (_state_est <= 100000) {
                progress = std::min<uint8_t>(
                    static_cast<uint8_t>(_best_g.size() * 100 / std::max(_state_est, size_t(1))),
                    static_cast<uint8_t>(99));
            } else {
                progress = std::min<uint8_t>(100 - 100 / (1 + _explored / 10000), 99);
            }
            ctx.report_progress(progress, ProgressStatus::Exploring);
        }

        // ─── Precompute max levels for delta heuristic ─────────────────────
        search_utils::precompute_max(current.ids, _pool, *_ench_reg,
                                      _h_max, _h_dirty);

        // ─── Expand current state ────────────────────────────────────────
        const auto& cur_ids = current.ids;
        const size_t n = cur_ids.size();

        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                if (i == j) continue;

                if (!_forge_engine.is_forgeable(_pool[cur_ids[i]], _pool[cur_ids[j]]))
                    continue;

                // ── Phase A: Lightweight pre-pruning (zero Item copies) ──
                int32_t est = _forge_engine.estimate_forge_cost(
                    _pool[cur_ids[i]], _pool[cur_ids[j]], reg);
                int32_t child_est_g = current.g + est;
                if (_best_solution_cost != INT32_MAX && child_est_g > _best_solution_cost) {
                    ++_diag.pruned_by_cost;
                    ctx.incr_nodes_pruned();
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
                    ctx.incr_nodes_pruned();
                    continue;
                }

                // ── Phase B: Real forge (only survivors from Phase A) ────
                ItemID old_base_id = cur_ids[i];
                ItemID old_sac_id  = cur_ids[j];
                Item forged = _pool[old_base_id];
                int32_t real_cost = _forge_engine.forge_into(forged, _pool[old_sac_id], reg);
                int32_t child_g = current.g + real_cost;
                ctx.incr_steps_forged();

                // Real cost may exceed estimate — recheck
                if (_best_solution_cost != INT32_MAX && child_g > _best_solution_cost)
                    continue;

                // Add forged Item to pool
                ItemID new_base_id = _pool.add(std::move(forged));
                if (new_base_id == INVALID_ITEM_ID) continue;  // pool full
                child_ids[base_in_child] = new_base_id;  // NOW we have real state

                // Symmetry breaking: sort non-equipment items so (A,B) and (B,A)
                // produce the same canonical state, reducing best_g duplicates.
                if (child_ids.size() > 2)
                    std::sort(child_ids.begin() + 1, child_ids.end());

                // ── Phase C: heuristic + best_g + enqueue (real post-forge) ─
                int32_t child_h_val = _delta_h(current.h, forged, _pool[old_sac_id]);
                int32_t child_fv = child_g + child_h_val;
                if (_best_solution_cost != INT32_MAX && child_fv > _best_solution_cost) {
                    ++_diag.pruned_by_f;
                    ctx.incr_nodes_pruned();
                    continue;
                }

                size_t child_hash = _hash_ids(child_ids);
                if (int32_t* cg = _best_g.find(child_hash)) {
                    if (*cg <= child_g) {
                        ++_diag.pruned_by_best_g;
                        ctx.incr_nodes_pruned();
                        continue;
                    }
                }
                _best_g[child_hash] = child_g;

                // Step node
                _step_pool.push_back({
                    current.step_idx,
                    old_base_id,
                    old_sac_id,
                    real_cost
                });
                int32_t step_idx = static_cast<int32_t>(_step_pool.size()) - 1;

                open_set.push(PriorityEntry{
                    SearchState{child_g, child_h_val, child_hash, step_idx, std::move(child_ids)},
                    child_fv
                });
            }
        }
    }

    // ─── Exit diagnostics ────────────────────────────────────────────────
    _diag.explored_count = _explored;
    _diag.best_g_entries = _best_g.size();
    _diag.final_bound = _best_solution_cost;
    _diag.step_pool_used = _step_pool.size();
    _diag.step_pool_capacity = _step_pool.capacity();
    _diag.items_pool_used = _pool.size();
    _diag.items_pool_capacity = _pool.capacity();
    _diag.open_set_pending = open_set.size();
    _diag.solution_cost = _best_solution_cost;
    _diag.estimated_peak_bytes =
        static_cast<int64_t>(_pool.capacity()) * static_cast<int64_t>(sizeof(Item))
      + static_cast<int64_t>(_step_pool.capacity()) * static_cast<int64_t>(sizeof(StepNode))
      + static_cast<int64_t>(open_heap_cap) * static_cast<int64_t>(sizeof(PriorityEntry));
    if (ctx.is_cancelled()) {
        ctx.report_progress(100, ProgressStatus::Cancelled);
        _diag.status = "Cancelled";
    } else {
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
        _diag.status = "CompleteNoSolution";
    }
    ctx.set_exit_diagnostics(_diag);
}

// ─── _restore_and_execute ───────────────────────────────────────────────

void AStarAlgorithm::_restore_and_execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    ctx.report_progress(0, ProgressStatus::Starting);

    _forge_engine.set_config(input.config);
    _ench_reg = &input.ench_reg;
    _target = input.target;
    _target_level_map.assign(_ench_reg->size(), 0);
    for (const auto& t : _target)
        _target_level_map[t.id] = t.level;

    _max_solutions = input.search.max_solutions;
    _max_search_time = input.search.max_search_time;
    _budget = AStarMemoryBudget::from_memory_mb(
        input.search.memory_mb > 0 ? input.search.memory_mb : 2048,
        static_cast<int32_t>(input.items.size()));

    _pool.set_max(_budget.max_items_pool);

    // Recompute state estimate for progress reporting
    {
        _state_est = 64;
        if (input.items.size() > 1) {
            size_t f = 1;
            for (size_t k = 2; k <= input.items.size() && f <= (1u << 20); ++k) f *= k;
            _state_est = std::max<size_t>(64, f);
        }
    }

    // Reset diagnostics
    _diag = AStarDiagnostics{};
    _diag.initial_bound = _best_solution_cost;

    // Pre-allocate scratch buffers for heuristic
    if (_h_max.size() < _ench_reg->size())
        _h_max.assign(_ench_reg->size(), 0);
    if (_h_buf.size() < _ench_reg->size())
        _h_buf.assign(_ench_reg->size(), 0);
    _h_dirty.clear();

    // Rebuild open_set from restored _open_heap
    size_t open_heap_cap = _open_heap.capacity();
    std::priority_queue<
        PriorityEntry,
        std::vector<PriorityEntry>,
        std::greater<>
    > open_set(std::greater<>{}, std::move(_open_heap));

    // _best_g and _explored are already populated by deserialize

    // Report restored progress immediately so observers see the current state
    if (_state_est <= 100000) {
        uint8_t restored_progress = static_cast<uint8_t>(
            _best_g.size() * 100 / std::max(_state_est, size_t(1)));
        if (restored_progress > 0 && restored_progress < 100)
            ctx.report_progress(restored_progress, ProgressStatus::Exploring);
    }

    auto t0 = std::chrono::steady_clock::now();

    // ─── Main loop (copied from execute()) ──────────────────────────────
    while (!open_set.empty() && !ctx.is_cancelled()) {
        ctx.wait_if_paused();

        SearchState current = std::move(
            const_cast<PriorityEntry&>(open_set.top())).state;
        open_set.pop();

        size_t cur_h = current.hash;
        if (int32_t* bg = _best_g.find(cur_h)) {
            if (*bg < current.g)
                continue;
        }

        if (_meets_target(current.ids[0])) {
            ++_solutions_found;
            if (current.g < _best_solution_cost)
                _best_solution_cost = current.g;

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
            ctx.report_solution(steps);
            ctx.report_progress(100, ProgressStatus::Complete);

            _diag.explored_count = _explored;
            _diag.best_g_entries = _best_g.size();
            _diag.final_bound = _best_solution_cost;
            _diag.step_pool_used = _step_pool.size();
            _diag.step_pool_capacity = _step_pool.capacity();
            _diag.items_pool_used = _pool.size();
            _diag.items_pool_capacity = _pool.capacity();
            _diag.solution_cost = _best_solution_cost;
            _diag.open_set_pending = open_set.size();
            _diag.estimated_peak_bytes =
                static_cast<int64_t>(_pool.capacity()) * static_cast<int64_t>(sizeof(Item))
              + static_cast<int64_t>(_step_pool.capacity()) * static_cast<int64_t>(sizeof(StepNode))
              + static_cast<int64_t>(open_heap_cap) * static_cast<int64_t>(sizeof(PriorityEntry));
            _diag.status = "Complete";
            ctx.set_exit_diagnostics(_diag);
            return;
        }

        _explored++;
        ctx.incr_nodes_visited();
        if (_explored >= _budget.max_explored) break;

        if (_explored % 1024 == 0) {
            if (_max_search_time.count() > 0) {
                auto elapsed = std::chrono::steady_clock::now() - t0;
                if (elapsed > _max_search_time) break;
            }
            if (_max_solutions > 0 && _solutions_found >= _max_solutions) break;
        }

        if ((_explored & 0x3F) == 0) {  // every 64 states
            uint8_t progress;
            if (_state_est <= 100000) {
                progress = std::min<uint8_t>(
                    static_cast<uint8_t>(_best_g.size() * 100 / std::max(_state_est, size_t(1))),
                    static_cast<uint8_t>(99));
            } else {
                progress = std::min<uint8_t>(100 - 100 / (1 + _explored / 10000), 99);
            }
            ctx.report_progress(progress, ProgressStatus::Exploring);
        }

        // ─── Precompute max levels for delta heuristic ─────────────────
        search_utils::precompute_max(current.ids, _pool, *_ench_reg,
                                      _h_max, _h_dirty);

        // ─── Expand current state ────────────────────────────────────
        const auto& cur_ids = current.ids;
        const size_t n = cur_ids.size();

        for (size_t i = 0; i < n; ++i) {
            for (size_t j = 0; j < n; ++j) {
                if (i == j) continue;

                if (!_forge_engine.is_forgeable(_pool[cur_ids[i]], _pool[cur_ids[j]]))
                    continue;

                // Phase A: Lightweight pre-pruning
                int32_t est = _forge_engine.estimate_forge_cost(
                    _pool[cur_ids[i]], _pool[cur_ids[j]], *_ench_reg);
                int32_t child_est_g = current.g + est;
                if (_best_solution_cost != INT32_MAX && child_est_g > _best_solution_cost) {
                    ++_diag.pruned_by_cost;
                    ctx.incr_nodes_pruned();
                    continue;
                }

                std::vector<ItemID> child_ids = cur_ids;
                child_ids.erase(child_ids.begin() + static_cast<std::ptrdiff_t>(j));
                size_t base_in_child = (i > j) ? i - 1 : i;

                bool at_cap = (_step_pool.size() >= _budget.max_step_pool
                            || open_set.size() >= static_cast<size_t>(_budget.max_open_set));
                if (at_cap) {
                    ++_diag.pruned_by_caps;
                    ctx.incr_nodes_pruned();
                    continue;
                }

                // Phase B: Real forge
                ItemID old_base_id = cur_ids[i];
                ItemID old_sac_id  = cur_ids[j];
                Item forged = _pool[old_base_id];
                int32_t real_cost = _forge_engine.forge_into(forged, _pool[old_sac_id], *_ench_reg);
                int32_t child_g = current.g + real_cost;
                ctx.incr_steps_forged();

                if (_best_solution_cost != INT32_MAX && child_g > _best_solution_cost)
                    continue;

                ItemID new_base_id = _pool.add(std::move(forged));
                if (new_base_id == INVALID_ITEM_ID) continue;
                child_ids[base_in_child] = new_base_id;

                if (child_ids.size() > 2)
                    std::sort(child_ids.begin() + 1, child_ids.end());

                // Phase C: heuristic + best_g + enqueue
                int32_t child_h_val = _delta_h(current.h, forged, _pool[old_sac_id]);
                int32_t child_fv = child_g + child_h_val;
                if (_best_solution_cost != INT32_MAX && child_fv > _best_solution_cost) {
                    ++_diag.pruned_by_f;
                    ctx.incr_nodes_pruned();
                    continue;
                }

                size_t child_hash = _hash_ids(child_ids);
                if (int32_t* cg = _best_g.find(child_hash)) {
                    if (*cg <= child_g) {
                        ++_diag.pruned_by_best_g;
                        ctx.incr_nodes_pruned();
                        continue;
                    }
                }
                _best_g[child_hash] = child_g;

                _step_pool.push_back({
                    current.step_idx,
                    old_base_id,
                    old_sac_id,
                    real_cost
                });
                int32_t step_idx = static_cast<int32_t>(_step_pool.size()) - 1;

                open_set.push(PriorityEntry{
                    SearchState{child_g, child_h_val, child_hash, step_idx, std::move(child_ids)},
                    child_fv
                });
            }
        }
    }

    // ─── Exit diagnostics ────────────────────────────────────────────
    _diag.explored_count = _explored;
    _diag.best_g_entries = _best_g.size();
    _diag.final_bound = _best_solution_cost;
    _diag.step_pool_used = _step_pool.size();
    _diag.step_pool_capacity = _step_pool.capacity();
    _diag.items_pool_used = _pool.size();
    _diag.items_pool_capacity = _pool.capacity();
    _diag.open_set_pending = open_set.size();
    _diag.solution_cost = _best_solution_cost;
    _diag.estimated_peak_bytes =
        static_cast<int64_t>(_pool.capacity()) * static_cast<int64_t>(sizeof(Item))
      + static_cast<int64_t>(_step_pool.capacity()) * static_cast<int64_t>(sizeof(StepNode))
      + static_cast<int64_t>(open_heap_cap) * static_cast<int64_t>(sizeof(PriorityEntry));
    if (ctx.is_cancelled()) {
        ctx.report_progress(100, ProgressStatus::Cancelled);
        _diag.status = "Cancelled";
    } else {
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
        _diag.status = "CompleteNoSolution";
    }
    ctx.set_exit_diagnostics(_diag);
}

// ─── _x_export_best_g ───────────────────────────────────────────────────

void AStarAlgorithm::_x_export_best_g(ByteStreamWriter& w) const {
    w.u32(static_cast<uint32_t>(_best_g.size()));
    for (size_t i = 0; i < _best_g.bucket_count(); ++i) {
        if (_best_g.occupied_at(i)) {
            w.u64(static_cast<uint64_t>(_best_g.key_at(i)));
            w.i32(_best_g.val_at(i));
        }
    }
}

// ─── _x_import_best_g ───────────────────────────────────────────────────

void AStarAlgorithm::_x_import_best_g(ByteStreamReader& r) {
    _best_g.clear();
    uint32_t count = r.u32();
    if (count > compact_serial::MAX_SERIAL_BEST_G) { r.set_fail(); return; }
    for (uint32_t i = 0; i < count; ++i) {
        size_t key = static_cast<size_t>(r.u64());
        int32_t val = r.i32();
        if (!r.ok()) break;
        _best_g[key] = val;
    }
}
