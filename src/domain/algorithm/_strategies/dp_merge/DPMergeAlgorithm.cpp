#include "DPMergeAlgorithm.h"
#include "DPMergeStateSerializer.h"
#include "common/utils/thread/ThreadPool.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/components/SearchUtils.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace algorithm {

using namespace algorithm;

// ─── Frontier::insert ──────────────────────────────────────────────────────

void DPMergeAlgorithm::Frontier::insert(ParetoEntry entry) {
    for (auto& existing : entries) {
        if (existing.ppn == entry.ppn &&
            existing.item.type == entry.item.type &&
            existing.item.enchs == entry.item.enchs) {
            if (entry.cost < existing.cost) {
                existing = std::move(entry);
            }
            return;
        }
    }
    entries.push_back(std::move(entry));
}

// ─── canonicalize ──────────────────────────────────────────────────────────

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

// ─── solve (recursive DP with memoization) ─────────────────────────────────
//
// For large N (≥ 12) the outer partition-mask loop is parallelised via
// ThreadPool::shared() — each chunk processes its masks independently,
// using thread-safe cache access (shared_mutex) for the recursive solve()
// calls.  Small N uses the original sequential path.

DPMergeAlgorithm::Frontier DPMergeAlgorithm::solve(std::vector<Item> items, bool parallelize) {
    if (items.size() == 1) {
        Frontier f;
        f.entries.push_back(ParetoEntry{0, items[0].ppn, std::move(items[0]), StepTree{}});
        return f;
    }

    if (items.size() == 2) {
        Frontier f;
        const Item& a = items[0];
        const Item& b = items[1];

        auto make_leaf = [&](const Item& base, const Item& sac,
                              int32_t cost) -> StepTree {
            auto node = std::make_shared<StepTree::Node>(
                EnchStep{base, sac, cost}, nullptr, nullptr, 1);
            return StepTree{std::move(node)};
        };

        if (_forge_engine.is_forgeable(a, b)) {
            auto [result, cost] = _forge_engine.forge(a, b, *_ench_reg);
            f.insert(ParetoEntry{cost, result.ppn, std::move(result),
                                 make_leaf(a, b, cost)});
        }
        if (_forge_engine.is_forgeable(b, a)) {
            auto [result, cost] = _forge_engine.forge(b, a, *_ench_reg);
            f.insert(ParetoEntry{cost, result.ppn, std::move(result),
                                 make_leaf(b, a, cost)});
        }
        return f;
    }

    // ── Check memoisation cache (thread-safe) ──────────────────────
    {
        std::shared_lock lock(_cache_mutex);
        auto it = _cache.find(items);
        if (it != _cache.end())
            return it->second;
    }

    const size_t n = items.size();

    if (n >= 64) return {};
    if (n > 20) {
        std::unique_lock lock(_cache_mutex);
        _cache[std::move(items)] = Frontier{};
        return {};
    }

    Frontier result;
    const uint64_t limit = 1ULL << n;

    // ─── forge_pair lambda (shared by both paths) ──────────────────
    // Creates a StepTree node linking to the parents' trees instead of
    // copying the full steps vector (zero-copy sharing).
    auto forge_pair = [&](Frontier& local,
                           const ParetoEntry& a,
                           const ParetoEntry& b) {
        auto make_tree = [&](const Item& base, const Item& sac,
                              int32_t cost) -> StepTree {
            auto node = std::make_shared<StepTree::Node>(
                EnchStep{base, sac, cost},
                a.step_tree.root_ptr(),
                b.step_tree.root_ptr(),
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
            local.insert(ParetoEntry{total, new_item.ppn,
                                     std::move(new_item),
                                     make_tree(a.item, b.item, cost)});
        }
        if (_forge_engine.is_forgeable(b.item, a.item)) {
            auto [new_item, cost] = _forge_engine.forge(
                b.item, a.item, *_ench_reg);
            int64_t total = static_cast<int64_t>(a.cost)
                          + b.cost + cost;
            if (total > std::numeric_limits<int32_t>::max())
                total = std::numeric_limits<int32_t>::max();
            local.insert(ParetoEntry{total, new_item.ppn,
                                     std::move(new_item),
                                     make_tree(b.item, a.item, cost)});
        }
    };

    // ─── Parallel mask loop (top-level only, via parallel_for) ─────────
    // For N < 14 the parallel overhead can exceed the benefit on 32-way
    // systems, so we keep those sequential.
    if (parallelize && n >= 14) {
        auto& pool = besq::ThreadPool::shared();
        std::mutex result_mutex;

        parallel_for(pool, uint64_t{1}, limit - 1,
            [&](uint64_t mask) {
                size_t k = __builtin_popcountll(mask);
                if (k * 2 > n) return;
                if ((n & 1) == 0 && k * 2 == n && !(mask & 1)) return;

                // Build left/right partition (thread-local)
                std::vector<Item> left, right;
                left.reserve(n);
                right.reserve(n);
                for (size_t i = 0; i < n; ++i) {
                    if (mask & (1ULL << i))
                        left.push_back(items[i]);
                    else
                        right.push_back(items[i]);
                }
                canonicalize(left);
                canonicalize(right);

                // Recursive solve — sequential only (avoids nested parallel_for)
                Frontier left_f  = solve(std::move(left), false);
                Frontier right_f = solve(std::move(right), false);
                if (left_f.empty() || right_f.empty()) return;

                // Cartesian product (sequential — frontiers are small)
                Frontier local;
                for (const auto& entry_a : left_f.entries)
                    for (const auto& entry_b : right_f.entries)
                        forge_pair(local, entry_a, entry_b);

                // Merge into global result (mutex-protected)
                if (!local.empty()) {
                    std::lock_guard<std::mutex> lk(result_mutex);
                    for (auto& e : local.entries)
                        result.insert(std::move(e));
                }
            });
    } else {
        // ─── Sequential mask loop (original path for N < 12) ────────
        std::vector<Item> left_buf, right_buf;
        left_buf.reserve(n);
        right_buf.reserve(n);

        for (uint64_t mask = 1; mask + 1 < limit; ++mask) {
            size_t k = __builtin_popcountll(mask);

            if (k * 2 > n) continue;
            if ((n & 1) == 0 && k * 2 == n && !(mask & 1)) continue;

            left_buf.clear();
            right_buf.clear();

            for (size_t i = 0; i < n; ++i) {
                if (mask & (1ULL << i))
                    left_buf.push_back(items[i]);
                else
                    right_buf.push_back(items[i]);
            }

            canonicalize(left_buf);
            canonicalize(right_buf);

            Frontier left_f  = solve(std::move(left_buf));
            Frontier right_f = solve(std::move(right_buf));

            if (left_f.empty() || right_f.empty())
                continue;

            for (const auto& entry_a : left_f.entries)
                for (const auto& entry_b : right_f.entries)
                    forge_pair(result, entry_a, entry_b);
        }
    }

    // ── Store in cache (thread-safe) ───────────────────────────────
    {
        std::unique_lock lock(_cache_mutex);
        if (_cache.find(items) == _cache.end() && _cache.size() < MAX_CACHE_ENTRIES)
            _cache[std::move(items)] = result;
    }
    return result;
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
    _ench_reg  = &input.ench_reg;
    _target.clear();
    for (const auto& e : input.target.enchs)
        _target.push_back(e);

    if (ctx.is_restored())
        return;  // _cache already restored by serializer

    // Fresh start: clear memoisation cache
    _cache.clear();
    _diag = AlgorithmDiagnostics{};
}

// ─── execute ──────────────────────────────────────────────────────────────

void DPMergeAlgorithm::execute(AlgorithmInput input,
                                ExecutionContext& ctx) {
    _forge_engine.set_config(input.f_config);

    // Configure thread pool concurrency from search config
    if (input.s_config.max_threads > 0)
        besq::ThreadPool::set_concurrency(input.s_config.max_threads);

    if (!ctx.is_restored()) {
        _cache.clear();
        _diag = AlgorithmDiagnostics{};
    }

    ctx.report_progress(0, ProgressStatus::Starting);

    const auto& items = input.items;

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

    Frontier frontier = solve(std::move(mutable_items), true);

    const ParetoEntry* best = nullptr;
    for (const auto& entry : frontier.entries) {
        if (entry.item.type == ItemType::Equip &&
            meets_target(entry.item, _target))
        {
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
    // Fitted from benchmark data:  t(e) ≈ 0.004 × 2.7^e
    // (Catalan-number DP, confirmed 1ms@7 / 20ms@9 / 389ms@12 / 14s@16)
    double r = 0.004 * std::pow(2.7, static_cast<double>(ench_count));
    return r;
}


} // namespace algorithm
