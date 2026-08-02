#include "BBDpAlgorithm.h"
#include "common/utils/thread/ThreadPool.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/resolvers/IResolver.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace algorithm {

using namespace algorithm;

// ─── Frontier::insert (Pareto domination + optional beam) ────────────────

void BBDpAlgorithm::Frontier::insert(ParetoEntry entry, int32_t beam_width) {
    const Item& item = entry.item;

    // Cross-ppn Pareto domination over the same (type, enchset):
    //   - an existing entry with ≤ ppn and ≤ cost dominates `entry` → drop it;
    //   - `entry` with ≤ ppn and ≤ cost dominates an existing entry → remove it.
    // Future merge cost depends only on (type, enchset, ppn), so the dominated
    // entry can never lead to a strictly better result.
    for (auto it = entries.begin(); it != entries.end();) {
        if (it->item.type == item.type && it->item.enchs == item.enchs) {
            if (it->ppn <= entry.ppn && it->cost <= entry.cost) {
                ++dropped;  // candidate eliminated by Pareto domination
                return;     // entry is dominated
            }
            if (entry.ppn <= it->ppn && entry.cost <= it->cost) {
                ++dropped;  // existing entry pruned (replaced by a dominant one)
                it = entries.erase(it);
                continue;
            }
        }
        ++it;
    }
    entries.push_back(std::move(entry));

    // Beam: keep only the `beam_width` cheapest entries (heuristic — loses
    // optimality for beam_width > 0, traded for speed on large N).
    if (beam_width > 0 && entries.size() > static_cast<size_t>(beam_width)) {
        std::sort(entries.begin(), entries.end(),
            [](const ParetoEntry& a, const ParetoEntry& b) { return a.cost < b.cost; });
        entries.resize(static_cast<size_t>(beam_width));
    }
}

// ─── canonicalize ─────────────────────────────────────────────────────────
//
// NOTE (review M2): the sort key uses `enchs.hash()` (a 64-bit hash) rather
// than full content.  With the bitmask memo key, a hash collision between two
// DISTINCT items would (a) make this comparator a non-strict-weak-ordering
// (std::sort UB) and (b) conflate two distinct subproblems in the memo cache.
// The old vector<Item> memo key compared full content and was immune to (b).
// A 64-bit EnchSet hash collision with real data is astronomically unlikely
// (the previous code had the same (a) exposure).  Left as-is; revisit if the
// registry ever exceeds ~2^32 enchant combinations.

void BBDpAlgorithm::canonicalize(std::vector<Item>& items) noexcept {
    std::sort(items.begin(), items.end(),
        [](const Item& a, const Item& b) {
            auto order = [](ItemType t) -> uint8_t {
                switch (t) {
                    case ItemType::Equip: return 0;
                    case ItemType::Book:  return 1;
                    default:              return 2;
                }
            };
            uint8_t oa = order(a.type);
            uint8_t ob = order(b.type);
            if (oa != ob) return oa < ob;
            if (a.ppn != b.ppn) return a.ppn < b.ppn;
            return a.enchs.hash() < b.enchs.hash();
        });
}

// ─── compute_ub: deterministic balanced-merge bound (hamming-style) ───────

static int popcount32(int x) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcount(static_cast<unsigned>(x));
#else
    int c = 0;
    while (x) { c += x & 1; x >>= 1; }
    return c;
#endif
}

static std::vector<int> dup_floor_members(int j, int n) {
    std::vector<int> result;
    result.reserve(static_cast<size_t>(std::max(0, n)));
    for (int i = 0; i < n; ++i)
        if (popcount32(i) == j) result.push_back(i);
    return result;
}

int32_t BBDpAlgorithm::compute_ub(std::vector<Item> items) {
    // Deterministic hamming-style construction: at each PPN tier, sort by
    // estimated forge cost desc (expensive books merge early), arrange into a
    // popcount-balanced bracket, pairwise-forge, bubble results to the next
    // tier.  Mirrors HammingAlgorithm's tier logic to produce a valid complete
    // solution whose total cost serves as the B&B upper bound.  Records the
    // merge steps into _ub_steps (used as an anytime fallback).  Returns
    // INT32_MAX when the construction does not reach the target.
    _ub_steps.clear();
    normalize_base_equipment(items);
    if (items.empty()) return INT32_MAX;

    int max_ppn = 0;
    for (const auto& it : items) max_ppn = std::max<int>(max_ppn, it.ppn);

    std::vector<std::vector<Item>> tiers(static_cast<size_t>(max_ppn) + 1);
    for (auto& it : items) tiers[static_cast<size_t>(it.ppn)].push_back(std::move(it));

    int64_t total = 0;
    for (size_t tier = 0; tier < tiers.size(); ++tier) {
        if (tiers[tier].size() <= 1) {
            if (tiers[tier].size() == 1) {
                size_t nt = tier + 1;
                if (nt < tiers.size()) {
                    tiers[nt].push_back(std::move(tiers[tier][0]));
                    tiers[tier].clear();
                }
            }
            continue;
        }

        auto& cur = tiers[tier];
        std::sort(cur.begin(), cur.end(), [&](const Item& a, const Item& b) {
            bool a_eq = (a.type == ItemType::Equip);
            bool b_eq = (b.type == ItemType::Equip);
            if (a_eq != b_eq) return a_eq;
            return _forge_engine.estimate_forge_cost(a, a, *_ench_reg)
                 > _forge_engine.estimate_forge_cost(b, b, *_ench_reg);
        });

        const size_t n = cur.size();
        std::vector<Item> arranged(n);
        arranged[0] = std::move(cur[0]);
        size_t src = 1;
        for (int j = 1; src < n; ++j) {
            auto members = dup_floor_members(j, static_cast<int>(n));
            for (int pos : members) {
                if (pos == 0 || src >= n) continue;
                arranged[static_cast<size_t>(pos)] = std::move(cur[src++]);
            }
        }

        std::vector<Item> next_items;
        while (arranged.size() >= 2) {
            Item base = std::move(arranged.front());
            arranged.erase(arranged.begin());
            Item sac = std::move(arranged.front());
            arranged.erase(arranged.begin());

            if (!_forge_engine.is_forgeable(base, sac)) {
                if (_forge_engine.is_forgeable(sac, base)) {
                    std::swap(base, sac);
                } else {
                    next_items.push_back(std::move(base));
                    next_items.push_back(std::move(sac));
                    continue;
                }
            }
            Item base_before = base;
            Item sac_before  = sac;
            int32_t cost = _forge_engine.forge_into(base, sac, *_ench_reg);
            if (cost == INT32_MAX) return INT32_MAX;
            total += cost;
            if (total > INT32_MAX) total = INT32_MAX;
            _ub_steps.emplace_back(std::move(base_before), std::move(sac_before), base, cost);
            next_items.push_back(std::move(base));
        }
        if (arranged.size() == 1)
            next_items.push_back(std::move(arranged[0]));

        size_t nt = tier + 1;
        if (nt >= tiers.size()) tiers.resize(nt + 1);
        for (auto& it : next_items) tiers[nt].push_back(std::move(it));
    }

    // Only trust the bound if the construction actually reaches the target.
    bool found_equip = false;
    Item final_item;
    for (auto& tier : tiers) {
        for (auto& it : tier) {
            if (it.type == ItemType::Equip) {
                final_item = std::move(it);
                found_equip = true;
            }
        }
    }
    if (!found_equip || !meets_target(final_item, _target))
        return INT32_MAX;
    return static_cast<int32_t>(total);
}

// ─── cache_get / cache_put / _prepare_cache ───────────────────────────────

void BBDpAlgorithm::_prepare_cache(size_t n) {
    _using_flat = (n <= FLAT_CACHE_MAX_BITS);
    _owners.clear();
    if (_using_flat) {
        const size_t slots = size_t{1} << n;  // n ≤ 20 → ≤ 1M slots
        if (!_flat_cache || _flat_capacity != slots) {
            _flat_cache = std::make_unique<std::atomic<Frontier*>[]>(slots);
            _flat_capacity = slots;
        }
        // Reinitialize every slot to nullptr (atomic, single-threaded here).
        for (size_t i = 0; i < slots; ++i)
            _flat_cache[i].store(nullptr, std::memory_order_relaxed);
    }
}

const BBDpAlgorithm::Frontier* BBDpAlgorithm::cache_get(uint64_t mask) const noexcept {
    if (_using_flat) {
        // Lock-free: single atomic load.  `mask` < 2^n by construction.
        return _flat_cache[mask].load(std::memory_order_acquire);
    }
    std::shared_lock lock(_cache_mutex);
    auto it = _cache.find(mask);
    return (it != _cache.end()) ? it->second.get() : nullptr;
}

const BBDpAlgorithm::Frontier& BBDpAlgorithm::cache_put(uint64_t mask, std::unique_ptr<Frontier> f) {
    // Aggregate this subproblem's Pareto drops into the global counter (one
    // relaxed atomic per subproblem — spec Tier 1).  `f` is not yet moved.
    _dp_pareto_dropped.fetch_add(f->dropped, std::memory_order_relaxed);
    if (_using_flat) {
        // Keep the frontier alive BEFORE publishing, so the published pointer
        // can never dangle (even if push_back throws): ownership lives in
        // _owners for the whole pass.  A wasted owner entry on a lost CAS is
        // harmless (freed at pass end).
        Frontier* mine;
        {
            std::lock_guard<std::mutex> lk(_owners_mutex);
            _owners.push_back(std::move(f));
            mine = _owners.back().get();
        }
        Frontier* expected = nullptr;
        if (_flat_cache[mask].compare_exchange_strong(expected, mine,
                                                      std::memory_order_acq_rel,
                                                      std::memory_order_acquire))
            return *mine;
        return *expected;  // lost the race: a concurrent equivalent frontier wins
    }
    std::unique_lock lock(_cache_mutex);
    auto it = _cache.find(mask);
    if (it != _cache.end())
        return *it->second;  // concurrent insert won — equivalent

    // Only grow the map while under the cap (the old `operator[]` grew it past
    // MAX_CACHE_ENTRIES unconditionally, unbounded).  Past the cap, hold the
    // frontier in the pass-lifetime overflow arena — memory stays bounded for
    // the flat-cache range (n ≤ 20) and the map only grows to its cap for the
    // rare n > 20 fallback, where the search degrades to recomputation on
    // overflow-arena hits instead of caching.
    if (_cache.size() < MAX_CACHE_ENTRIES) {
        auto [slot, inserted] = _cache.emplace(mask, std::move(f));
        return *slot->second;
    }
    _overflow.push_back(std::move(f));
    return *_overflow.back();
}

// ─── solve: recursive partition DP with B&B + cap + Pareto ───────────────

const BBDpAlgorithm::Frontier& BBDpAlgorithm::solve(uint64_t mask,
                                                    int32_t max_step_cost,
                                                    int32_t beam_width,
                                                    bool parallelize,
                                                    bool final_level,
                                                    ExecutionContext &ctx) {
    // Memoisation (frontiers pruned at this run's dynamic `_best_cost`).
    if (const Frontier* hit = cache_get(mask))
        return *hit;

    const size_t n = static_cast<size_t>(__builtin_popcountll(mask));

    // Snapshot of the dynamic B&B bound at this subproblem's entry.  The bound
    // is only ever tightened by a genuine full-set solution (final_level), so
    // it is constant throughout a non-final solve; for the top-level solve the
    // post-loop re-prune below corrects any mid-loop tightening.  Hoisting the
    // load here removes ~one atomic load per forged pair from the hot path.
    const int32_t bound_snapshot = _best_cost.load(std::memory_order_relaxed);

    // Tier-1 prune counters (spec §5): accumulate locally in this subproblem,
    // flush to the global once at the end.  Never a per-operation atomic.
    uint64_t cap_pruned = 0, bound_pruned = 0;

    if (n == 1) {
        auto f = std::make_unique<Frontier>();
        const Item& it = _base_items[static_cast<size_t>(__builtin_ctzll(mask))];
        f->entries.push_back(ParetoEntry{0, it.ppn, 0, it, StepTree{}});
        return cache_put(mask, std::move(f));
    }

    if (n == 2) {
        auto f = std::make_unique<Frontier>();
        uint64_t m = mask;
        const size_t i0 = static_cast<size_t>(__builtin_ctzll(m)); m &= m - 1;
        const size_t i1 = static_cast<size_t>(__builtin_ctzll(m));
        const Item& a = _base_items[i0];
        const Item& b = _base_items[i1];
        auto make_leaf = [&](const Item& base, const Item& sac,
                             const Item& result, int32_t cost) -> StepTree {
            auto node = std::make_shared<StepTree::Node>(
                EnchStep{base, sac, result, cost}, nullptr, nullptr, 1);
            return StepTree{std::move(node)};
        };
        auto try_forge = [&](const Item& base, const Item& sac) {
            if (!_forge_engine.is_forgeable(base, sac)) return;
            auto [result, cost] = _forge_engine.forge(base, sac, *_ench_reg);
            if (cost == INT32_MAX) return;
            if (max_step_cost > 0 && cost > max_step_cost) { ++cap_pruned; return; }
            if (static_cast<int64_t>(cost) > bound_snapshot) { ++bound_pruned; return; }  // keep == bound (may be optimal)
            // Only a genuine full-set solution may tighten the anytime bound.
            // A proper-subset "complete" item (redundant books) must not prune
            // the top-level frontier (see review finding I2).
            if (final_level && result.type == ItemType::Equip && meets_target(result, _target)) {
                int32_t seen = _best_cost.load(std::memory_order_relaxed);
                while (cost < seen &&
                       !_best_cost.compare_exchange_weak(seen, cost, std::memory_order_relaxed)) {}
            }
            auto tree = make_leaf(base, sac, result, cost);
            f->insert(ParetoEntry{cost, result.ppn, cost,
                                  std::move(result), std::move(tree)}, beam_width);
        };
        try_forge(a, b);
        try_forge(b, a);
        // Flush this n==2 subproblem's own prunes before the early return —
        // n==2 leaves are the most numerous in the recursion, so their counts
        // would otherwise be silently dropped (review finding #1).
        _dp_cap_pruned.fetch_add(cap_pruned, std::memory_order_relaxed);
        _dp_bound_pruned.fetch_add(bound_pruned, std::memory_order_relaxed);
        return cache_put(mask, std::move(f));
    }

    // No hard n cap: the Executor's max_search_time timeout bounds the search
    // (a cancelled subproblem returns an empty frontier below).
    if (ctx.is_cancelled())
        return cache_put(mask, std::make_unique<Frontier>());

    Frontier result;

    auto combine = [&](Frontier& local, const ParetoEntry& a, const ParetoEntry& b,
                       uint64_t& cap_cnt, uint64_t& bound_cnt) {
        if (a.cost + b.cost > bound_snapshot) { ++bound_cnt; return; }  // any completion > bound can't improve
        auto make_tree = [&](const std::shared_ptr<StepTree::Node>& base_tree,
                             const std::shared_ptr<StepTree::Node>& sac_tree,
                             const Item& base, const Item& sac,
                             const Item& result, int32_t cost) -> StepTree {
            auto node = std::make_shared<StepTree::Node>(
                EnchStep{base, sac, result, cost}, base_tree, sac_tree,
                a.step_tree.size() + b.step_tree.size() + 1);
            return StepTree{std::move(node)};
        };
        auto try_forge = [&](const Item& base, const Item& sac,
                             const std::shared_ptr<StepTree::Node>& base_tree,
                             const std::shared_ptr<StepTree::Node>& sac_tree) {
            if (!_forge_engine.is_forgeable(base, sac)) return;
            auto [new_item, cost] = _forge_engine.forge(base, sac, *_ench_reg);
            if (cost == INT32_MAX) return;
            if (max_step_cost > 0 && cost > max_step_cost) { ++cap_cnt; return; }
            int64_t total = a.cost + b.cost + cost;
            if (total > bound_snapshot) { ++bound_cnt; return; }  // keep == bound (may be optimal)
            // Only a genuine full-set solution may tighten the anytime bound
            // (see review finding I2).
            if (final_level && new_item.type == ItemType::Equip && meets_target(new_item, _target)) {
                int32_t seen = _best_cost.load(std::memory_order_relaxed);
                while (total < seen &&
                       !_best_cost.compare_exchange_weak(seen, static_cast<int32_t>(total),
                                                         std::memory_order_relaxed)) {}
            }
            int32_t max_step = std::max(a.max_step, b.max_step);
            max_step = std::max(max_step, cost);
            auto tree = make_tree(base_tree, sac_tree, base, sac, new_item, cost);
            local.insert(ParetoEntry{total, new_item.ppn, max_step,
                                     std::move(new_item), std::move(tree)},
                         beam_width);
        };
        try_forge(a.item, b.item, a.step_tree.root_ptr(), b.step_tree.root_ptr());
        try_forge(b.item, a.item, b.step_tree.root_ptr(), a.step_tree.root_ptr());
    };

    // Partition enumeration over the set bits of `mask`.  Each unordered
    // 2-partition appears once: keep subsets with |left| ≤ n/2; for even n +
    // equal halves require the lowest set bit to be in `left` (canonical order
    // of `_base_items` is fixed, so bit 0 of the full set is a stable anchor).
    const uint64_t low_bit = mask & (~mask + 1);

    auto process_subset = [&](Frontier& out, uint64_t left,
                              uint64_t& cap_cnt, uint64_t& bound_cnt) {
        ctx.wait_if_paused();
        if (ctx.is_cancelled()) return;
        const size_t k = __builtin_popcountll(left);
        if (k * 2 > n) return;
        if ((n & 1) == 0 && k * 2 == n && !(left & low_bit)) return;

        const uint64_t right = mask & ~left;
        const Frontier& left_f  = solve(left,  max_step_cost, beam_width,
                                        /*parallelize=*/false, /*final_level=*/false, ctx);
        if (!left_f.empty()) {
            const Frontier& right_f = solve(right, max_step_cost, beam_width,
                                            /*parallelize=*/false, /*final_level=*/false, ctx);
            if (!right_f.empty()) {
                for (const auto& ea : left_f.entries)
                    for (const auto& eb : right_f.entries)
                        combine(out, ea, eb, cap_cnt, bound_cnt);
            }
        }
    };

    if (parallelize && n >= PARALLEL_THRESHOLD) {
        auto& pool = besq::ThreadPool::shared();
        std::mutex result_mutex;
        ctx.report_progress(20, ProgressStatus::Exploring);
        parallel_for(pool, uint64_t{1}, mask, [&](uint64_t left) {
            if ((left & ~mask) != 0) return;      // not a subset of `mask`
            uint64_t p_cap = 0, p_bound = 0;
            Frontier local;
            process_subset(local, left, p_cap, p_bound);
            {
                std::lock_guard<std::mutex> lk(result_mutex);
                // Carry this partition's Pareto drops into `result` so they are
                // aggregated at the top-level cache_put (review finding #2).
                // local itself is never cache_put — only its entries are merged.
                result.dropped += local.dropped;
                if (!local.empty())
                    for (auto& e : local.entries)
                        result.insert(std::move(e), beam_width);
            }
            // Flush per-mask prunes into the global counters (Tier 1: one
            // relaxed atomic per partition, never per forge).
            _dp_cap_pruned.fetch_add(p_cap, std::memory_order_relaxed);
            _dp_bound_pruned.fetch_add(p_bound, std::memory_order_relaxed);
        });
    } else {
        // Enumerate all proper non-empty subsets of `mask` (skip 0 and mask).
        uint64_t left = (mask - 1) & mask;  // largest proper subset
        uint64_t count = 0;
        const uint64_t total = static_cast<uint64_t>(1) << n;
        while (left != 0) {
            process_subset(result, left, cap_pruned, bound_pruned);
            ++count;
            if ((count & 0xFF) == 0)
                ctx.report_progress(5 + static_cast<uint8_t>(90 * count / total),
                                    ProgressStatus::Exploring);
            left = (left - 1) & mask;
        }
    }

    // Flush this subproblem's own (base-case + sequential-loop) prunes.  For
    // the parallel top-level this is just the n==2 base counts (0 for n ≥ 14);
    // the per-partition counts were already flushed inside the workers.
    _dp_cap_pruned.fetch_add(cap_pruned, std::memory_order_relaxed);
    _dp_bound_pruned.fetch_add(bound_pruned, std::memory_order_relaxed);

    // Drop complete entries that cannot beat the bound (keep those equal to it).
    const int32_t dyn_bound = _best_cost.load(std::memory_order_relaxed);
    if (dyn_bound < INT32_MAX) {
        result.entries.erase(
            std::remove_if(result.entries.begin(), result.entries.end(),
                [dyn_bound](const ParetoEntry& e) { return e.cost > dyn_bound; }),
            result.entries.end());
    }

    // Admissible PPN lower-bound: a non-final entry must be merged at least
    // once more, and that next merge pays ≥ penalty(ppn), so an entry with
    // cost + penalty(ppn) > bound can never reach a solution ≤ bound.
    if (!final_level && dyn_bound < INT32_MAX) {
        result.entries.erase(
            std::remove_if(result.entries.begin(), result.entries.end(),
                [&](const ParetoEntry& e) {
                    int64_t forced = e.cost + _forge_engine.penalty_cost(e.item.ppn);
                    return forced > dyn_bound;
                }),
            result.entries.end());
    }

    return cache_put(mask, std::make_unique<Frontier>(std::move(result)));
}

// ─── evaluate ─────────────────────────────────────────────────────────────

double BBDpAlgorithm::evaluate(int16_t ench_count) const noexcept {
    // B&B bound + cap + Pareto collapse the Catalan blow-up; placeholder fit
    // (discounted from dp_merge's 2.9^e).  Recalibrate on the benchmark.
    return 0.002 * std::pow(2.5, static_cast<double>(ench_count));
}

// ─── execute ──────────────────────────────────────────────────────────────

void BBDpAlgorithm::execute(const AlgorithmInput &input, ExecutionContext &ctx) {
    _forge_engine.set_config(input.config.forge);
    _ench_reg = &input.registry;
    _target.clear();
    for (const auto& e : input.target.enchs)
        _target.push_back(e);
    _diag = PartitionDpDiagnostics{};
    // Reset per-pass diagnostic accumulators (execute() may be re-run).
    _dp_cap_pruned.store(0, std::memory_order_relaxed);
    _dp_bound_pruned.store(0, std::memory_order_relaxed);
    _dp_pareto_dropped.store(0, std::memory_order_relaxed);

    // Configure thread-pool concurrency from search config.
    if (input.config.search.max_threads > 0)
        besq::ThreadPool::set_concurrency(input.config.search.max_threads);

    ctx.report_progress(0, ProgressStatus::Starting);

    auto items = get_resolver()->resolve(input);
    normalize_base_equipment(items);

    if (items.empty()) {
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
        _diag.status = "CompleteNoSolution";
        ctx.set_exit_diagnostics(_diag);
        return;
    }
    if (meets_target(items[0], _target)) {
        ctx.report_solution({});
        ctx.report_progress(100, ProgressStatus::GoalAlreadyMet);
        _diag.status = "GoalAlreadyMet";
        ctx.set_exit_diagnostics(_diag);
        return;
    }
    if (items.size() <= 1) {
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
        _diag.status = "CompleteNoSolution";
        ctx.set_exit_diagnostics(_diag);
        return;
    }

    const int32_t cap        = input.config.search.max_step_cost;
    const int32_t beam_width = input.config.search.beam_width;

    // Upper bound: the internal hamming-style construction (also fills
    // _ub_steps for the anytime fallback).  A warm-up SearchConfig.initial_bound
    // is only trusted when it is ≥ this achievable cost — a user-supplied bound
    // below the optimum would prune it and wrongly yield "no solution".
    int32_t internal = compute_ub(items);
    const bool ub_ok = (internal != INT32_MAX);  // construction reached the target
    int32_t bound = input.config.search.initial_bound;
    if (bound <= 0 || bound == INT32_MAX) {
        bound = internal;
    } else if (internal != INT32_MAX) {
        bound = std::max(bound, internal);
    }
    if (bound < INT32_MAX) {
        // Tighten with a node-limited DFS (always to an achievable cost ≥ optimum).
        int64_t node_limit = 50'000;
        std::vector<int16_t> h_buf, h_dirty;
        bound = search_utils::dfs_bound(items, 0, bound, node_limit,
                                        _forge_engine, *_ench_reg, _target,
                                        h_buf, h_dirty);
    }

    canonicalize(items);
    _base_items = items;
    const uint64_t full_mask = (items.size() >= 64)
        ? UINT64_MAX : ((static_cast<uint64_t>(1) << items.size()) - 1);

    // Tier-0 diagnostics: initial bound / UB gap (spec §5).
    _diag.dp_ub_cost    = (ub_ok ? internal : INT32_MAX);
    _diag.initial_bound = bound;

    // Tier-0 state-space metrics derived from the memo cache at pass end
    // (zero hot-path cost).  Also snapshots the Tier-1 aggregates.
    auto fill_cache_diag = [&]() {
        _diag.dp_cap_pruned     = _dp_cap_pruned.load(std::memory_order_relaxed);
        _diag.dp_bound_pruned   = _dp_bound_pruned.load(std::memory_order_relaxed);
        _diag.dp_pareto_dropped = _dp_pareto_dropped.load(std::memory_order_relaxed);
        _diag.final_bound       = _best_cost.load(std::memory_order_relaxed);
        if (_using_flat && _flat_cache) {
            uint64_t solved = 0, max_f = 0;
            for (size_t i = 0; i < _flat_capacity; ++i)
                if (auto* fp = _flat_cache[i].load(std::memory_order_relaxed)) {
                    ++solved;
                    if (fp->entries.size() > max_f) max_f = fp->entries.size();
                }
            _diag.dp_subproblems_solved    = solved;
            _diag.dp_cache_slots           = _flat_capacity;
            _diag.dp_cache_hits            = _flat_capacity - solved;
            _diag.dp_max_frontier_size     = static_cast<uint32_t>(max_f);
            _diag.normalized_explored_states = static_cast<int64_t>(solved);
        } else {
            // Map path: count map entries + the pass-lifetime overflow arena
            // (cache_put past MAX_CACHE_ENTRIES lands there).
            const size_t solved = _cache.size() + _overflow.size();
            _diag.dp_subproblems_solved = solved;
            _diag.dp_cache_slots        = solved;
            _diag.dp_cache_hits         = 0;  // map path: hit rate not tracked
            _diag.normalized_explored_states = static_cast<int64_t>(solved);
        }
    };

    auto find_best = [&](const Frontier& f) -> const ParetoEntry* {
        const ParetoEntry* best = nullptr;
        for (const auto& e : f.entries)
            if (e.item.type == ItemType::Equip && meets_target(e.item, _target))
                if (!best || e.cost < best->cost)
                    best = &e;
        return best;
    };

    // ── Pass A (strict): constrained search with the strongest prune ─────
    // ≤cap is a SOFT constraint but its structural prune is the strongest
    // (§3.5: penalty(6)=63>39 ⇒ ppn≤5 ⇒ tree height ≤~6).  Run it FIRST
    // (design doc §5.2): when a fully-≤cap solution exists it is both the
    // fastest to find and the preferred answer.  The unconstrained search
    // (Pass B) only runs when no ≤cap solution exists.
    const ParetoEntry* best = nullptr;
    _diag.dp_pass_b_ran = false;

    if (cap > 0) {
        // Seed the B&B bound only if the hamming construction is itself fully
        // ≤cap (an infeasible construction's cost is not an achievable bound
        // inside the constrained space).  Otherwise start unbound — the cap
        // structural prune carries the search.
        int32_t feas_bound = INT32_MAX;
        if (ub_ok && !_ub_steps.empty()) {
            bool hamming_feasible = true;
            for (const auto& s : _ub_steps)
                if (s.cost > cap) { hamming_feasible = false; break; }
            if (hamming_feasible)
                feas_bound = internal;
        }

        _cache.clear();
        _overflow.clear();
        _prepare_cache(items.size());
        _best_cost.store(feas_bound, std::memory_order_relaxed);
        const Frontier& feas_frontier = solve(full_mask, cap, beam_width,
                                              /*parallelize=*/true, /*final_level=*/true, ctx);
        fill_cache_diag();  // Tier-0 state-space metrics from Pass A's cache
        if (ctx.is_cancelled()) {
            _diag.status = "Cancelled";
            ctx.set_exit_diagnostics(_diag);
            ctx.report_progress(100, ProgressStatus::Cancelled);
            return;
        }
        best = find_best(feas_frontier);
    }

    // ── Pass B (relax): unconstrained optimum — only when no ≤cap solution ─
    if (!best) {
        _cache.clear();
        _overflow.clear();
        _prepare_cache(items.size());
        _best_cost.store(bound, std::memory_order_relaxed);
        _diag.dp_pass_b_ran = true;
        const Frontier& frontier = solve(full_mask, 0, beam_width,
                                         /*parallelize=*/true, /*final_level=*/true, ctx);
        fill_cache_diag();  // Tier-0 state-space metrics from Pass B's cache
        if (ctx.is_cancelled()) {
            _diag.status = "Cancelled";
            ctx.set_exit_diagnostics(_diag);
            ctx.report_progress(100, ProgressStatus::Cancelled);
            return;
        }
        best = find_best(frontier);
    }

    if (best) {
        _diag.solution_cost = static_cast<int32_t>(best->cost);
        _diag.status        = "Complete";
        ctx.set_exit_diagnostics(_diag);
        ctx.report_solution(best->step_tree.materialize());
        ctx.report_progress(100, ProgressStatus::Complete);
    } else if (ub_ok && !_ub_steps.empty()) {
        // Anytime fallback: the exact search gave up (e.g. n > 28 or a search
        // failure), but the deterministic hamming construction reached the
        // target — report it rather than "no solution".
        int64_t ub_cost = 0;
        for (const auto& s : _ub_steps) ub_cost += s.cost;
        _diag.solution_cost = static_cast<int32_t>(std::min<int64_t>(ub_cost, INT32_MAX));
        _diag.status        = "Complete";
        ctx.set_exit_diagnostics(_diag);
        ctx.report_solution(_ub_steps);
        ctx.report_progress(100, ProgressStatus::Complete);
    } else {
        _diag.status = "CompleteNoSolution";
        ctx.set_exit_diagnostics(_diag);
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
    }
}

} // namespace algorithm
