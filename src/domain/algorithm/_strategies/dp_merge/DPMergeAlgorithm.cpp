#include "DPMergeAlgorithm.h"
#include "DPMergeStateSerializer.h"
#include "common/utils/thread/ThreadPool.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/resolvers/IResolver.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

namespace algorithm {

using namespace algorithm;

// ─── Frontier::insert (Pareto domination) ─────────────────────────────────

void DPMergeAlgorithm::Frontier::insert(ParetoEntry entry) {
    const Item& item = entry.item;

    // Cross-ppn Pareto domination over the same (type, enchset):
    //   - an existing entry with ≤ ppn and ≤ cost dominates `entry` → drop it;
    //   - `entry` with ≤ ppn and ≤ cost dominates an existing entry → remove it.
    // Future merge cost depends only on (type, enchset, ppn), so the dominated
    // entry can never lead to a strictly better result.  (Ported from bb_dp —
    // the previous insert only replaced an exact (ppn, type, enchset) match,
    // keeping dominated entries and bloating the frontier.)
    for (auto it = entries.begin(); it != entries.end();) {
        if (it->item.type == item.type && it->item.enchs == item.enchs) {
            if (it->ppn <= entry.ppn && it->cost <= entry.cost) {
                ++dropped;  // candidate eliminated by Pareto domination
                return;     // entry is dominated
            }
            if (entry.ppn <= it->ppn && entry.cost <= it->cost) {
                ++dropped;  // existing entry pruned (replaced by a dominant one)
                it = entries.erase(it);  // entry dominates existing
                continue;
            }
        }
        ++it;
    }
    entries.push_back(std::move(entry));
}

// ─── canonicalize ─────────────────────────────────────────────────────────

void DPMergeAlgorithm::canonicalize(std::vector<Item>& items) noexcept {
    std::sort(items.begin(), items.end(),
        [](const Item& a, const Item& b) {
            auto order = [](ItemType t) -> uint8_t {
                switch (t) {
                    case ItemType::Equip:  return 0;
                    case ItemType::Book:   return 1;
                    default:               return 2;
                }
            };
            uint8_t oa = order(a.type);
            uint8_t ob = order(b.type);
            if (oa != ob) return oa < ob;
            if (a.ppn != b.ppn) return a.ppn < b.ppn;
            return a.enchs.hash() < b.enchs.hash();
        });
}

// ─── cache_get / cache_put / _prepare_cache ───────────────────────────────
//
// n ≤ 20: flat lock-free cache — a single atomic load on the read path, no
// shared_mutex contention even under the top-level parallel_for.  n > 20 (rare,
// dp_merge bails there anyway) uses the mutex-protected map fallback.

void DPMergeAlgorithm::_prepare_cache(size_t n) {
    _using_flat = (n <= FLAT_CACHE_MAX_BITS);
    _owners.clear();
    if (_using_flat) {
        const size_t slots = size_t{1} << n;  // n ≤ 20 → ≤ 1M slots
        if (!_flat_cache || _flat_capacity != slots) {
            _flat_cache = std::make_unique<std::atomic<Frontier*>[]>(slots);
            _flat_capacity = slots;
        }
        // Reinitialise every slot to nullptr (atomic, single-threaded here).
        for (size_t i = 0; i < slots; ++i)
            _flat_cache[i].store(nullptr, std::memory_order_relaxed);
    }
}

const DPMergeAlgorithm::Frontier* DPMergeAlgorithm::cache_get(uint64_t mask) const noexcept {
    if (_using_flat) {
        // Lock-free: single atomic load.  `mask` < 2^n by construction.
        return _flat_cache[mask].load(std::memory_order_acquire);
    }
    std::shared_lock lock(_cache_mutex);
    auto it = _cache.find(mask);
    return (it != _cache.end()) ? it->second.get() : nullptr;
}

const DPMergeAlgorithm::Frontier& DPMergeAlgorithm::cache_put(uint64_t mask, std::unique_ptr<Frontier> f) {
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

    if (_cache.size() < MAX_CACHE_ENTRIES) {
        auto [slot, inserted] = _cache.emplace(mask, std::move(f));
        return *slot->second;
    }
    // Past the cap: hold the frontier in the pass-lifetime overflow arena.
    _owners.push_back(std::move(f));
    return *_owners.back();
}

// ─── solve (recursive DP with memoization) ─────────────────────────────────
//
// `mask` identifies the current subset of `_base_items`; each unordered
// 2-partition appears once (the canonical-order anchor from `low_bit`).  For
// large N (≥ 14) the outer partition-mask loop is parallelised via
// ThreadPool::shared() — each chunk processes its masks independently and
// publishes results through the lock-free cache.

const DPMergeAlgorithm::Frontier& DPMergeAlgorithm::solve(uint64_t mask, bool parallelize,
                                                          ExecutionContext& ctx) {
    // Memoisation (lock-free hit on the flat path).
    if (const Frontier* hit = cache_get(mask))
        return *hit;

    const size_t n = static_cast<size_t>(__builtin_popcountll(mask));

    if (n == 1) {
        auto f = std::make_unique<Frontier>();
        const Item& it = _base_items[static_cast<size_t>(__builtin_ctzll(mask))];
        f->entries.push_back(ParetoEntry{0, it.ppn, it, StepTree{}});
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

        if (_forge_engine.is_forgeable(a, b)) {
            auto [result, cost] = _forge_engine.forge(a, b, *_ench_reg);
            auto tree = make_leaf(a, b, result, cost);
            f->insert(ParetoEntry{cost, result.ppn, std::move(result), std::move(tree)});
        }
        if (_forge_engine.is_forgeable(b, a)) {
            auto [result, cost] = _forge_engine.forge(b, a, *_ench_reg);
            auto tree = make_leaf(b, a, result, cost);
            f->insert(ParetoEntry{cost, result.ppn, std::move(result), std::move(tree)});
        }
        return cache_put(mask, std::move(f));
    }

    // No hard n cap: the Executor's max_search_time timeout bounds the search
    // (a cancelled subproblem returns an empty frontier below).
    if (ctx.is_cancelled())
        return cache_put(mask, std::make_unique<Frontier>());

    Frontier result;

    // ─── forge_pair lambda (shared by both paths) ──────────────────
    // Creates a StepTree node linking to the parents' trees instead of
    // copying the full steps vector (zero-copy sharing).
    auto forge_pair = [&](Frontier& local,
                           const ParetoEntry& a,
                           const ParetoEntry& b) {
        auto make_tree = [&](const std::shared_ptr<StepTree::Node>& base_tree,
                             const std::shared_ptr<StepTree::Node>& sac_tree,
                             const Item& base, const Item& sac,
                             const Item& result, int32_t cost) -> StepTree {
            auto node = std::make_shared<StepTree::Node>(
                EnchStep{base, sac, result, cost}, base_tree, sac_tree,
                a.step_tree.size() + b.step_tree.size() + 1);
            return StepTree{std::move(node)};
        };

        if (_forge_engine.is_forgeable(a.item, b.item)) {
            auto [new_item, cost] = _forge_engine.forge(
                a.item, b.item, *_ench_reg);
            int64_t total = static_cast<int64_t>(a.cost)
                          + b.cost + cost;
            if (total > std::numeric_limits<int32_t>::max())
                total = std::numeric_limits<int32_t>::max();
            auto tree = make_tree(a.step_tree.root_ptr(), b.step_tree.root_ptr(),
                                  a.item, b.item, new_item, cost);
            local.insert(ParetoEntry{total, new_item.ppn,
                                     std::move(new_item), std::move(tree)});
        }
        if (_forge_engine.is_forgeable(b.item, a.item)) {
            auto [new_item, cost] = _forge_engine.forge(
                b.item, a.item, *_ench_reg);
            int64_t total = static_cast<int64_t>(a.cost)
                          + b.cost + cost;
            if (total > std::numeric_limits<int32_t>::max())
                total = std::numeric_limits<int32_t>::max();
            auto tree = make_tree(b.step_tree.root_ptr(), a.step_tree.root_ptr(),
                                  b.item, a.item, new_item, cost);
            local.insert(ParetoEntry{total, new_item.ppn,
                                     std::move(new_item), std::move(tree)});
        }
    };

    // Canonical order anchor: the lowest set bit of the current subset decides
    // which half a 2-partition belongs to (only one orientation is enumerated).
    const uint64_t low_bit = mask & (~mask + 1);

    auto process_subset = [&](Frontier& out, uint64_t left) {
        ctx.wait_if_paused();
        if (ctx.is_cancelled()) return;
        const size_t k = __builtin_popcountll(left);
        if (k * 2 > n) return;
        if ((n & 1) == 0 && k * 2 == n && !(left & low_bit)) return;

        const uint64_t right = mask & ~left;
        const Frontier& left_f  = solve(left,  /*parallelize=*/false, ctx);
        if (!left_f.empty()) {
            const Frontier& right_f = solve(right, /*parallelize=*/false, ctx);
            if (!right_f.empty()) {
                for (const auto& ea : left_f.entries)
                    for (const auto& eb : right_f.entries)
                        forge_pair(out, ea, eb);
            }
        }
    };

    // ─── Parallel mask loop (top-level only, via parallel_for) ─────────
    // For N < 14 the parallel overhead can exceed the benefit on 32-way
    // systems, so we keep those sequential.
    if (parallelize && n >= 14) {
        auto& pool = besq::ThreadPool::shared();
        std::mutex result_mutex;

        // Stoppable: on cancel the chunk loops stop at their next index, so
        // the 2^n mask range is not drained element-by-element after a
        // timeout (n=26 previously took ~4 minutes of unwind).
        parallel_for_stoppable(pool, uint64_t{1}, mask,
            [&](uint64_t left) {
                if ((left & ~mask) != 0) return;  // not a subset of `mask`
                Frontier local;
                process_subset(local, left);
                {
                    std::lock_guard<std::mutex> lk(result_mutex);
                    // Carry this partition's Pareto drops into `result` so they
                    // are aggregated at the top-level cache_put (review #2).
                    result.dropped += local.dropped;
                    if (!local.empty())
                        for (auto& e : local.entries)
                            result.insert(std::move(e));
                }
            },
            [&] { return ctx.is_cancelled(); });
    } else {
        // ─── Sequential mask loop (original path for N < 12) ────────
        uint64_t left = (mask - 1) & mask;  // largest proper submask of `mask`
        while (left != 0 && !ctx.is_cancelled()) {
            process_subset(result, left);
            left = (left - 1) & mask;
        }
    }

    return cache_put(mask, std::make_unique<Frontier>(std::move(result)));
}

// ─── Serialization support ─────────────────────────────────────────────────

IAlgorithmSerializer *DPMergeAlgorithm::get_serializer() noexcept {
    if (!_serializer)
        _serializer = std::make_unique<DPMergeStateSerializer>();
    return _serializer.get();
}
const IAlgorithmSerializer *DPMergeAlgorithm::get_serializer() const noexcept {
    return const_cast<DPMergeAlgorithm *>(this)->get_serializer();
}

// ─── init ─────────────────────────────────────────────────────────────────

void DPMergeAlgorithm::init(const AlgorithmInput &input, const ExecutionContext &ctx) {
    _ench_reg = &input.registry;
    _target   = input.target;

    if (ctx.is_restored())
        return;  // _cache already restored by serializer

    // Fresh start: clear memoisation cache
    _cache.clear();
    _diag = PartitionDpDiagnostics{};
    _dp_pareto_dropped.store(0, std::memory_order_relaxed);
}

// ─── execute ──────────────────────────────────────────────────────────────

void DPMergeAlgorithm::execute(const AlgorithmInput &input, ExecutionContext& ctx) {
    _forge_engine.set_config(input.config.forge);

    // Configure thread pool concurrency from search config
    if (input.config.search.max_threads > 0)
        besq::ThreadPool::set_concurrency(input.config.search.max_threads);

    if (!ctx.is_restored()) {
        _cache.clear();
        _diag = PartitionDpDiagnostics{};
        _dp_pareto_dropped.store(0, std::memory_order_relaxed);
    }

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

    std::vector<Item> mutable_items = items;
    canonicalize(mutable_items);

    // The mask-keyed cache is only valid when the freshly resolved base set is
    // identical to the base set the cache was built for (checkpoint/resume
    // assumes the same input).  Any mismatch — a changed input between save and
    // resume, or a checkpoint carrying no solve state — must recompute: the
    // flat cache is sized by the serialized base set, so a mismatched item
    // count would index past `_flat_capacity`, and same-count-but-different
    // items would return frontiers computed for another item order.
    if (!ctx.is_restored()) {
        _prepare_cache(mutable_items.size());
    } else if (mutable_items.size() != _base_items.size() ||
               mutable_items != _base_items) {
        _cache.clear();
        _prepare_cache(mutable_items.size());
    }

    // Base items + full-set mask: every subproblem is a subset of this mask,
    // so the flat cache (1 << n slots) covers all of them.
    _base_items = mutable_items;
    const uint64_t full_mask = (mutable_items.size() >= 64)
        ? UINT64_MAX : ((static_cast<uint64_t>(1) << mutable_items.size()) - 1);

    const Frontier& frontier = solve(full_mask, true, ctx);

    // Tier-0 diagnostics: derive state-space metrics from the memo cache
    // (zero hot-path cost — spec §5).
    _diag.dp_pareto_dropped = _dp_pareto_dropped.load(std::memory_order_relaxed);
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
        // (cache_put past MAX_CACHE_ENTRIES lands in `_owners`).
        const size_t solved = _cache.size() + _owners.size();
        _diag.dp_subproblems_solved = solved;
        _diag.dp_cache_slots        = solved;
        _diag.normalized_explored_states = static_cast<int64_t>(solved);
    }

    if (ctx.is_cancelled()) {
        _diag.status = "Cancelled";
        ctx.set_exit_diagnostics(_diag);
        ctx.report_progress(100, ProgressStatus::Cancelled);
        return;
    }

    const ParetoEntry* best = nullptr;
    for (const auto& entry : frontier.entries) {
        if (meets_target(entry.item, _target)) {
            if (!best || entry.cost < best->cost)
                best = &entry;
        }
    }

    if (best) {
        _diag.solution_cost = static_cast<int32_t>(best->cost);
        _diag.status        = "Complete";
        ctx.set_exit_diagnostics(_diag);
        ctx.report_solution(best->step_tree.materialize());
        ctx.report_progress(100, ProgressStatus::Complete);
    } else {
        _diag.status = "CompleteNoSolution";
        ctx.set_exit_diagnostics(_diag);
        ctx.report_progress(100, ProgressStatus::CompleteNoSolution);
    }
}


// ─── evaluate ──────────────────────────────────────────────────────────────────

double DPMergeAlgorithm::evaluate(int16_t ench_count) const noexcept {
    // Fitted from scaling benchmark (Release, netherite_sword 9-18 enchs):
    //   t(e) ≈ 5.467e-9 × 3.5357^e   seconds   (R²=0.9998, tail fit +30% safety)
    // Feasible ≤ 18 in the benchmark context; 19+ exceeds the 45 s budget.
    double r = 5.467e-9 * std::pow(3.5357, static_cast<double>(ench_count));
    return r;
}


} // namespace algorithm
