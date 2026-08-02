#include "BBDpAlgorithm.h"
#include "common/utils/thread/ThreadPool.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/resolvers/IResolver.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace algorithm {

using namespace algorithm;

// ─── Frontier::insert (Pareto domination + optional beam) ────────────────

BBDpAlgorithm::ParetoEntry* BBDpAlgorithm::Frontier::insert(ParetoEntry entry, int32_t beam_width) {
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
                return nullptr;  // entry is dominated
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
        // Beam re-sorting moves entries around, so a returned pointer would be
        // ambiguous; beam callers build trees eagerly, so nullptr is safe here.
        return nullptr;
    }
    return &entries.back();
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
    return std::popcount(static_cast<unsigned>(x));
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
        // Precompute each item's self-estimate once, then sort indices — the
        // previous comparator re-ran estimate_forge_cost() on every comparison
        // (O(n log n) full cost computations per tier).
        std::vector<int32_t> est(cur.size());
        for (size_t i = 0; i < cur.size(); ++i)
            est[i] = _forge_engine.estimate_forge_cost(cur[i], cur[i], *_ench_reg);
        std::vector<size_t> order(cur.size());
        for (size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            bool a_eq = (cur[a].type == ItemType::Equip);
            bool b_eq = (cur[b].type == ItemType::Equip);
            if (a_eq != b_eq) return a_eq;
            return est[a] > est[b];
        });
        std::vector<Item> ordered_cur;
        ordered_cur.reserve(cur.size());
        for (size_t i : order)
            ordered_cur.push_back(std::move(cur[i]));
        cur = std::move(ordered_cur);

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
    // The final holder must be the target item type (equipment or book).
    bool found_final = false;
    Item final_item;
    for (auto& tier : tiers) {
        for (auto& it : tier) {
            if (it.type == _target.type) {
                final_item = std::move(it);
                found_final = true;
            }
        }
    }
    if (!found_final || !meets_target(final_item, _target))
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

// ─── solve: bottom-up layered partition DP with B&B + cap + Pareto ───────
//
// Replaces the old recursive top-down solver.  Bottom-up computes the memo
// cache layer by layer (a mask of size k depends only on subfrontiers of
// smaller size, all cached by earlier layers), which lets EVERY layer run as
// an independent parallel_for with no nesting and no shared-result mutex.
// The old recursion only parallelised the top level, capping speedup at ~3×
// on 32 threads: the single largest subset (size n-1) was a serial
// bottleneck.  Each mask's frontier is built identically (same partitions,
// same cap / bound / Pareto prunes), so the optimum is unchanged.

const BBDpAlgorithm::Frontier& BBDpAlgorithm::solve(uint64_t mask,
                                                    int32_t max_step_cost,
                                                    int32_t beam_width,
                                                    bool parallelize,
                                                    bool final_level,
                                                    ExecutionContext &ctx) {
    if (const Frontier* hit = cache_get(mask))
        return *hit;

    const size_t n = static_cast<size_t>(std::popcount(mask));

    // Dynamic B&B bound at solve entry.  In bottom-up it is constant for all
    // layers (only top-layer full-set solutions tighten it), and the initial
    // bound is achievable ≥ optimum, so pruning against it stays admissible.
    const int32_t bound_snapshot = _best_cost.load(std::memory_order_relaxed);

    // ── combine: cartesian forge of two subfrontier entries into `local` ──
    auto combine = [&](Frontier& local, const ParetoEntry& a, const ParetoEntry& b,
                       uint64_t& cap_cnt, uint64_t& bound_cnt, bool final_layer) {
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
            // O(1) cap pre-check: forge cost = penalty(base.ppn) + penalty(sac.ppn)
            // + enchant_cost + repair_cost, and the last two are ≥ 0.  When the
            // penalty sum alone already exceeds the cap, the forge is guaranteed
            // capped — skip the full forge() computation.
            if (max_step_cost > 0 &&
                _forge_engine.penalty_cost(base.ppn) + _forge_engine.penalty_cost(sac.ppn) > max_step_cost) {
                ++cap_cnt;
                return;
            }
            auto [new_item, cost] = _forge_engine.forge(base, sac, *_ench_reg);
            if (cost == INT32_MAX) return;
            if (max_step_cost > 0 && cost > max_step_cost) { ++cap_cnt; return; }
            int64_t total = a.cost + b.cost + cost;
            if (total > bound_snapshot) { ++bound_cnt; return; }  // keep == bound (may be optimal)
            // Only a genuine full-set solution may tighten the anytime bound
            // (see review finding I2).
            if (final_layer && meets_target(new_item, _target)) {
                int32_t seen = _best_cost.load(std::memory_order_relaxed);
                while (total < seen &&
                       !_best_cost.compare_exchange_weak(seen, static_cast<int32_t>(total),
                                                         std::memory_order_relaxed)) {}
            }
            int32_t max_step = std::max(a.max_step, b.max_step);
            max_step = std::max(max_step, cost);
            // Build the merge-history StepTree lazily: the large majority of
            // candidates are dropped by Pareto domination, and each tree costs a
            // make_shared<StepTree::Node>.  Attach the tree only on survival —
            // the parent trees come from the frozen (cached) subfrontiers, so
            // referencing them here is stable.
            ParetoEntry entry{total, new_item.ppn, max_step, std::move(new_item), StepTree{}};
            if (beam_width > 0) {
                // Beam path builds eagerly: the entry may be re-sorted below
                // insert(), so a returned pointer would be ambiguous.  Rare.
                entry.step_tree = make_tree(base_tree, sac_tree, base, sac, entry.item, cost);
                local.insert(std::move(entry), beam_width);
            } else if (ParetoEntry* stored = local.insert(std::move(entry), beam_width)) {
                stored->step_tree = make_tree(base_tree, sac_tree, base, sac,
                                              stored->item, cost);
            }
        };
        try_forge(a.item, b.item, a.step_tree.root_ptr(), b.step_tree.root_ptr());
        try_forge(b.item, a.item, b.step_tree.root_ptr(), a.step_tree.root_ptr());
    };

    // ── forge_leaf: forge of a size-2 mask (two single items) ──
    auto forge_leaf = [&](Frontier& f, const Item& base, const Item& sac,
                          uint64_t& cap_cnt, uint64_t& bound_cnt, bool final_layer) {
        if (!_forge_engine.is_forgeable(base, sac)) return;
        if (max_step_cost > 0 &&
            _forge_engine.penalty_cost(base.ppn) + _forge_engine.penalty_cost(sac.ppn) > max_step_cost) {
            ++cap_cnt;
            return;
        }
        auto [result, cost] = _forge_engine.forge(base, sac, *_ench_reg);
        if (cost == INT32_MAX) return;
        if (max_step_cost > 0 && cost > max_step_cost) { ++cap_cnt; return; }
        if (static_cast<int64_t>(cost) > bound_snapshot) { ++bound_cnt; return; }  // keep == bound (may be optimal)
        if (final_layer && meets_target(result, _target)) {
            int32_t seen = _best_cost.load(std::memory_order_relaxed);
            while (cost < seen &&
                   !_best_cost.compare_exchange_weak(seen, cost, std::memory_order_relaxed)) {}
        }
        auto node = std::make_shared<StepTree::Node>(
            EnchStep{base, sac, result, cost}, nullptr, nullptr, 1);
        f.insert(ParetoEntry{cost, result.ppn, cost,
                             std::move(result), StepTree{std::move(node)}}, beam_width);
    };

    // Bucket every non-empty submask by popcount — one O(2^n) pass.
    std::vector<std::vector<uint64_t>> masks_by_size(n + 1);
    for (uint64_t m = mask; m; m = (m - 1) & mask)
        masks_by_size[static_cast<size_t>(std::popcount(m))].push_back(m);

    // Layer 1: single-item leaves.
    for (uint64_t m : masks_by_size[1]) {
        auto f = std::make_unique<Frontier>();
        const Item& it = _base_items[static_cast<size_t>(std::countr_zero(m))];
        f->entries.push_back(ParetoEntry{0, it.ppn, 0, it, StepTree{}});
        cache_put(m, std::move(f));
    }

    const bool use_parallel = parallelize && n >= PARALLEL_THRESHOLD;
    auto& pool = besq::ThreadPool::shared();

    // Layers 2..n.  Every mask in a layer depends only on cached subfrontiers
    // of strictly smaller size, so the layer's masks are independent → a
    // barrier-parallel_for per layer (no nesting, no shared result mutex).
    for (size_t k = 2; k <= n; ++k) {
        auto& layer = masks_by_size[k];
        const bool final_layer = (k == n) && final_level;
        auto body = [&](size_t idx) {
            ctx.wait_if_paused();
            if (ctx.is_cancelled()) return;
            const uint64_t sub = layer[idx];
            Frontier result;
            uint64_t cap_cnt = 0, bound_cnt = 0;

            if (k == 2) {
                uint64_t m = sub;
                const size_t i0 = static_cast<size_t>(std::countr_zero(m)); m &= m - 1;
                const size_t i1 = static_cast<size_t>(std::countr_zero(m));
                forge_leaf(result, _base_items[i0], _base_items[i1], cap_cnt, bound_cnt, final_layer);
                forge_leaf(result, _base_items[i1], _base_items[i0], cap_cnt, bound_cnt, final_layer);
            } else {
                // Partition enumeration over `sub`: each unordered 2-partition
                // once (|left| ≤ k/2; for even k require the lowest bit in left).
                const uint64_t low_bit = sub & (~sub + 1);
                uint64_t left = (sub - 1) & sub;
                while (left != 0) {
                    const size_t kk = std::popcount(left);
                    if (kk * 2 <= k && !((k & 1) == 0 && kk * 2 == k && !(left & low_bit))) {
                        const uint64_t right = sub & ~left;
                        const Frontier* left_f  = cache_get(left);
                        const Frontier* right_f = cache_get(right);
                        if (left_f && right_f && !left_f->empty() && !right_f->empty()) {
                            for (const auto& ea : left_f->entries)
                                for (const auto& eb : right_f->entries)
                                    combine(result, ea, eb, cap_cnt, bound_cnt, final_layer);
                        }
                    }
                    left = (left - 1) & sub;
                }
            }

            // Post-layer prunes (same semantics as the old recursion).
            const int32_t dyn_bound = _best_cost.load(std::memory_order_relaxed);
            if (dyn_bound < INT32_MAX) {
                result.entries.erase(
                    std::remove_if(result.entries.begin(), result.entries.end(),
                        [dyn_bound](const ParetoEntry& e) { return e.cost > dyn_bound; }),
                    result.entries.end());
            }
            // Admissible PPN lower-bound: a non-final entry must be merged at
            // least once more, and that next merge pays ≥ penalty(ppn).
            if (!final_layer && dyn_bound < INT32_MAX) {
                result.entries.erase(
                    std::remove_if(result.entries.begin(), result.entries.end(),
                        [&](const ParetoEntry& e) {
                            int64_t forced = e.cost + _forge_engine.penalty_cost(e.item.ppn);
                            return forced > dyn_bound;
                        }),
                    result.entries.end());
            }
            _dp_cap_pruned.fetch_add(cap_cnt, std::memory_order_relaxed);
            _dp_bound_pruned.fetch_add(bound_cnt, std::memory_order_relaxed);
            cache_put(sub, std::make_unique<Frontier>(std::move(result)));
        };
        if (use_parallel && layer.size() > 1) {
            parallel_for_stoppable(pool, size_t{0}, layer.size(), body,
                [&] { return ctx.is_cancelled(); });
        } else {
            for (size_t idx = 0; idx < layer.size(); ++idx)
                body(idx);
        }
        ctx.report_progress(static_cast<uint8_t>(k * 100 / n), ProgressStatus::Exploring);
        if (ctx.is_cancelled()) break;
    }

    if (const Frontier* hit = cache_get(mask))
        return *hit;
    return cache_put(mask, std::make_unique<Frontier>());  // cancelled — empty
}

// ─── evaluate ─────────────────────────────────────────────────────────────

double BBDpAlgorithm::evaluate(int16_t ench_count) const noexcept {
    // Fitted from scaling benchmark (Release, netherite_sword 9-19 enchs):
    //   t(e) ≈ 2.437e-8 × 3.2235^e   seconds   (R²=1.0, tail fit +30% safety)
    // Feasible ≤ 19 (19 = 87 s measured); 20+ exceeds the 300 s budget.
    return 2.437e-8 * std::pow(3.2235, static_cast<double>(ench_count));
}

// ─── execute ──────────────────────────────────────────────────────────────

void BBDpAlgorithm::execute(const AlgorithmInput &input, ExecutionContext &ctx) {
    _forge_engine.set_config(input.config.forge);
    _ench_reg = &input.registry;
    _target = input.target;
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
        // The internal compute_ub (hamming-style) is already within ~1 level of
        // optimal for typical inputs, so dp_bound_pruned is tiny and a large DFS
        // here is wasted work (~34% of the pre-pass profile at 50k nodes).
        int64_t node_limit = 2'000;  // cheap guard against pathologically loose UBs
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
            if (meets_target(e.item, _target))
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
