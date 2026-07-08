#include "DFSAlgorithm.h"
#include "utils/AlgorithmUtils.hpp"
#include "utils/ExpCalculator.hpp"
#include "utils/Serializer.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

DFSAlgorithm::StateKey DFSAlgorithm::make_state_key(
    const std::vector<ItemStack>& items) const
{
    StateKey key;
    key.penalties.reserve(items.size());

    size_t total_enchs = 0;
    for (const auto& item : items)
        total_enchs += item.enchantments.size();

    key.ench_ids.reserve(total_enchs);
    key.ench_levels.reserve(total_enchs);

    for (const auto& item : items) {
        key.penalties.push_back(item.prior_penalty);

        std::vector<Ench> sorted(item.enchantments.begin(), item.enchantments.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const Ench& a, const Ench& b) { return a.id < b.id; });
        for (const Ench& e : sorted) {
            key.ench_ids.push_back(e.id);
            key.ench_levels.push_back(e.level);
        }
    }

    return key;
}

std::vector<DFSAlgorithm::ForgePair> DFSAlgorithm::_collect_pairs(
    const std::vector<ItemStack>& items) const
{
    const size_t n = items.size();
    std::vector<ForgePair> pairs;
    pairs.reserve(n * (n - 1));

    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            if (!_forge_engine.is_forgeable(items[i], items[j]))
                continue;

            int32_t est = ItemStack::get_penalty_cost(items[i].prior_penalty)
                        + ItemStack::get_penalty_cost(items[j].prior_penalty);
            for (const Ench& e : items[j].enchantments) {
                est += e.level * e.get_multiplier(items[j].is_book());
            }
            pairs.push_back({i, j, est});
        }
    }

    std::sort(pairs.begin(), pairs.end(),
              [](const ForgePair& a, const ForgePair& b) { return a.est_cost < b.est_cost; });
    return pairs;
}

void DFSAlgorithm::execute(const AlgorithmInput& input, ExecutionContext& ctx) {
    ctx.report_progress(0.0, ProgressStatus::Starting);
    _input = &input;

    if (!_state_restored) {
        // ── Fresh start ──
        _best_cost   = INT32_MAX;
        _best_steps.clear();
        _current_steps.clear();
        _visited.clear();
        _stack.clear();
        _frame_pairs.clear();

        ItemStack start_item(
            input.target_item.equipment,
            input.original_ench,
            0
        );
        std::vector<ItemStack> items;
        items.reserve(1 + input.available_items.size());
        items.push_back(std::move(start_item));
        items.insert(items.end(), input.available_items.begin(), input.available_items.end());

        // Greedy upper bound for pruning
        if (items.size() > 1) {
            auto bound = AlgorithmUtils::book_first_merge(
                items[0], input.available_items, _forge_engine, ctx);
            _best_cost = bound.total_cost;
        }

        // Push root frame
        _stack.push_back({std::move(items), 0, 0, 0, {}, {}, 0, 0, false});
        _frame_pairs.emplace_back();  // pairs for root, computed lazily

    } else {
        // ── Restored from serialized state ──
        // _best_cost, _best_steps, _visited, _stack are pre-populated
        _current_steps.clear();
        _state_restored = false;
    }

    // ── Iterative search loop ──
    _dfs_iterative(ctx);

    // ── Report best solution ──
    if (!_best_steps.empty()) {
        ctx.report_solution_found(_best_steps);
    }
    ctx.report_progress(1.0, ProgressStatus::Complete);
}

void DFSAlgorithm::_dfs_iterative(ExecutionContext& ctx) {
    while (!_stack.empty() && !ctx.is_cancelled()) {
        ctx.wait_if_paused();

        auto& frame = _stack.back();

        // ── 1. Handle backtrack restore (after child returned) ──
        if (frame.has_backtrack) {
            size_t adj_base = (frame.sac_idx < frame.base_idx)
                ? frame.base_idx - 1 : frame.base_idx;
            frame.items[adj_base] = std::move(frame.saved_base);
            frame.items.insert(
                frame.items.begin() + frame.sac_idx, std::move(frame.saved_sac));
            _current_steps.resize(frame.saved_steps_size);
            frame.has_backtrack = false;
        }

        // ── 2. State memoization ──
        {
            StateKey key = make_state_key(frame.items);
            if (_visited.count(key)) {
                _stack.pop_back();
                _frame_pairs.pop_back();
                continue;
            }
            _visited.insert(std::move(key));
        }

        // ── 3. Goal check ──
        if (AlgorithmUtils::meets_target(frame.items[0], _input->target_item)) {
            if (_best_steps.empty() || frame.cost_so_far < _best_cost) {
                _best_cost  = frame.cost_so_far;
                _best_steps = _current_steps;  // steps leading to this state
            }
            _stack.pop_back();
            _frame_pairs.pop_back();
            continue;
        }

        // ── 4. Branch-and-bound pruning ──
        if (frame.cost_so_far + AlgorithmUtils::admissible_heuristic(
                frame.items[0].enchantments, _input->target_item.enchantments)
            >= _best_cost) {
            _stack.pop_back();
            _frame_pairs.pop_back();
            continue;
        }

        // ── 5. Lazy pair building ──
        auto& pairs = _frame_pairs.back();
        if (pairs.empty() && frame.pair_index == 0) {
            pairs = _collect_pairs(frame.items);
        }

        // ── 6. Find next valid pair ──
        if (frame.pair_index >= pairs.size()) {
            _stack.pop_back();
            _frame_pairs.pop_back();
            continue;
        }

        // ── 7. Execute forge and push child frame ──
        const auto& p = pairs[frame.pair_index++];

        // Save state for backtrack
        frame.saved_base = frame.items[p.i];
        frame.saved_sac  = frame.items[p.j];
        frame.base_idx   = p.i;
        frame.sac_idx    = p.j;

        // Apply forge (modifies items[p.i] in-place)
        int32_t step_cost = _forge_engine.forge_into(frame.items[p.i], frame.items[p.j]);

        _current_steps.push_back({
            frame.saved_base,
            frame.saved_sac,
            step_cost,
            ExpCalculator::level_to_exp(step_cost)
        });

        // Remove sacrifice
        frame.items.erase(frame.items.begin() + p.j);

        // Build child items (copy of parent's current state)
        std::vector<ItemStack> child_items = frame.items;

        // Push child frame
        _stack.push_back({
            std::move(child_items),
            frame.cost_so_far + step_cost,
            0,                          // pair_index starts at 0
            _current_steps.size(),      // saved_steps_size
            {}, {}, 0, 0, false         // no backtrack state yet
        });
        _frame_pairs.emplace_back();    // pairs for child, computed lazily

        // Mark current frame for backtrack when child returns
        frame.has_backtrack = true;
    }
}

// ─── Serialization ───

std::vector<uint8_t> DFSAlgorithm::serialize_state() const {
    Serializer s;

    s.u32(Serializer::MAGIC);
    s.u32(Serializer::VERSION);
    s.string(name());

    // Checkpoint data (best cost + best steps + visited)
    s.i32(_best_cost);
    s.u32(static_cast<uint32_t>(_best_steps.size()));
    for (const auto& step : _best_steps) s.write(step);

    s.u32(static_cast<uint32_t>(_visited.size()));
    for (const auto& key : _visited) {
        s.write(key.penalties);
        s.write(key.ench_ids);
        s.write(key.ench_levels);
    }

    // Full pause state: stack frames (0 = checkpoint only)
    s.u32(static_cast<uint32_t>(_stack.size()));
    for (size_t fi = 0; fi < _stack.size(); ++fi) {
        const auto& frame = _stack[fi];
        s.u32(static_cast<uint32_t>(frame.items.size()));
        for (const auto& item : frame.items) s.write(item);
        s.i32(frame.cost_so_far);
        s.u32(static_cast<uint32_t>(frame.pair_index));
        s.u8(frame.has_backtrack ? 1 : 0);
        if (frame.has_backtrack) {
            s.u32(static_cast<uint32_t>(frame.base_idx));
            s.u32(static_cast<uint32_t>(frame.sac_idx));
            s.write(frame.saved_base);
            s.write(frame.saved_sac);
        }
        s.u32(static_cast<uint32_t>(frame.saved_steps_size));
    }

    return s.data();
}

void DFSAlgorithm::deserialize_state(const std::vector<uint8_t>& data) {
    Deserializer d(data);

    uint32_t magic = d.u32();
    if (magic != Serializer::MAGIC || !d.ok()) return;
    uint32_t version = d.u32(); (void)version;
    std::string algo_name = d.string();
    if (algo_name != name() || !d.ok()) return;

    // Read checkpoint data
    _best_cost = d.i32();

    uint32_t step_count = d.u32();
    _best_steps.clear();
    _best_steps.reserve(step_count);
    for (uint32_t i = 0; i < step_count; ++i) {
        if (!d.ok()) break;
        _best_steps.push_back(d.read_step());
    }

    uint32_t visit_count = d.u32();
    _visited.clear();
    _visited.reserve(visit_count);
    for (uint32_t i = 0; i < visit_count; ++i) {
        if (!d.ok()) break;
        StateKey key;
        key.penalties  = d.read_i32_vec();
        key.ench_ids   = d.read_i32_vec();
        key.ench_levels = d.read_i32_vec();
        _visited.insert(std::move(key));
    }

    // Read stack frames (0 = checkpoint-only save)
    _stack.clear();
    _frame_pairs.clear();
    uint32_t frame_count = d.u32();
    for (uint32_t fi = 0; fi < frame_count && d.ok(); ++fi) {
        uint32_t item_count = d.u32();
        std::vector<ItemStack> items;
        items.reserve(item_count);
        for (uint32_t ii = 0; ii < item_count && d.ok(); ++ii)
            items.push_back(d.read_item_stack());

        int32_t cost_so_far    = d.i32();
        uint32_t pair_index    = d.u32();
        bool has_backtrack     = d.u8() != 0;

        ItemStack saved_base, saved_sac;
        size_t base_idx = 0, sac_idx = 0;
        if (has_backtrack) {
            base_idx  = static_cast<size_t>(d.u32());
            sac_idx   = static_cast<size_t>(d.u32());
            saved_base = d.read_item_stack();
            saved_sac  = d.read_item_stack();
        }

        size_t saved_steps_size = static_cast<size_t>(d.u32());

        _stack.push_back({
            std::move(items), cost_so_far, pair_index, saved_steps_size,
            std::move(saved_base), std::move(saved_sac),
            base_idx, sac_idx, has_backtrack
        });
        _frame_pairs.emplace_back();  // pairs recomputed lazily
    }

    if (d.ok())
        _state_restored = true;
}
