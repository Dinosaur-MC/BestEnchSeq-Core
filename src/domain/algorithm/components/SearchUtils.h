#pragma once
#include "domain/algorithm/forge_engine/IForgeEngine.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Item.h"
#include "ItemPool.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace algorithm {

// ─── Shared helpers ───────────────────────────────────────────────────

/// Check whether \p equipment satisfies all enchantments in \p target.
/// Returns true when every target enchantment is present at or above
/// the required level.
/// Accepts any iterable collection of Ench (EnchSet or EnchCollection).
template <typename EnchRange>
inline bool meets_target(const Item &equipment, const EnchRange &target) noexcept {
    for (const auto &t : target) {
        auto it = equipment.enchs.find(t.id);
        if (it == equipment.enchs.end() || it->level < t.level)
            return false;
    }
    return true;
}

/// Pool-based overload: resolves \p equip_id through \p pool.
template <typename EnchRange>
inline bool
meets_target(ItemPool::ItemID equip_id, const ItemPool &pool, const EnchRange &target) noexcept {
    return meets_target(pool[equip_id], target);
}

/// Shared search utilities extracted from AStarAlgorithm / IDAStarAlgorithm.
/// Keeps the duplicate-free implementations in one place.
namespace search_utils {

// ─── Fill max level per enchant from any ench-range accessor ──────────
//
// Calls `for_each_ench(fn)` which invokes `fn(ench_id, ench_level)` for
// each enchantment across all items.  This is the core loop shared by
// Heuristic::compute (pool-based) and HeuristicBasic::compute (item-based).

template <typename EnchEnumerator>
inline void fill_max_levels(
    EnchEnumerator &&enumerate_enchs, const EnchReg &reg, std::vector<int16_t> &buf,
    std::vector<int16_t> &dirty
) {
    if (buf.size() < reg.size())
        buf.assign(reg.size(), 0);
    dirty.clear();

    enumerate_enchs([&](int16_t id, int16_t level) {
        if (id < 0)
            return;
        if (level > buf[id]) {
            if (buf[id] == 0)
                dirty.push_back(id);
            buf[id] = level;
        }
    });
}

// ─── Precompute max level per enchant (pool-based) ─────────────────────

inline void precompute_max(
    const std::vector<ItemPool::ItemID> &ids, const ItemPool &pool, const EnchReg &reg,
    std::vector<int16_t> &h_max, std::vector<int16_t> &h_dirty
) {
    fill_max_levels(
        [&](auto &&yield) {
            for (auto id : ids)
                for (const auto &e : pool[id].enchs)
                    yield(e.id, e.level);
        },
        reg, h_max, h_dirty
    );
}

// ─── Compute heuristic h from precomputed max levels ──────────────────

inline int32_t
compute_h(const std::vector<Ench> &target, const EnchReg &reg, const std::vector<int16_t> &h_max) {
    int32_t h = 0;
    for (const auto &t : target) {
        int16_t have = h_max[t.id];
        if (have < t.level)
            h += (t.level - have) * reg[t.id].mul_b;
    }
    return h;
}

// ─── Limited DFS bound (item-vector based, no pool) ───────────────────
//
// Explores up to node_limit nodes to find a tight upper bound.
// Prioritises equipment-base forges (i == 0) to build complete solutions
// quickly, then explores book+book alternatives.

inline int32_t dfs_bound(
    std::vector<Item> items, int32_t g, int32_t best_cost, int64_t &node_limit,
    const IForgeEngine &forge_engine, const EnchReg &reg, const std::vector<Ench> &target,
    std::vector<int16_t> &h_buf, std::vector<int16_t> &h_dirty
) {
    if (node_limit <= 0)
        return best_cost;
    --node_limit;

    if (meets_target(items[0], target))
        return (g < best_cost) ? g : best_cost;

    // Inline heuristic (avoids circular dep with HeuristicBasic.h)
    search_utils::fill_max_levels(
        [&](auto &&yield) {
            for (const auto &item : items)
                for (const auto &e : item.enchs)
                    if (e.id >= 0)
                        yield(e.id, e.level);
        },
        reg, h_buf, h_dirty
    );
    int32_t h = search_utils::compute_h(target, reg, h_buf);
    for (auto id : h_dirty)
        h_buf[id] = 0;
    if (g + h >= best_cost)
        return best_cost;

    const size_t n = items.size();
    if (n < 2)
        return best_cost;

    struct Candidate {
        size_t i, j;
        int32_t est;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(n * (n - 1));

    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            if (i == j)
                continue;
            if (!forge_engine.is_forgeable(items[i], items[j]))
                continue;
            int32_t est = forge_engine.estimate_forge_cost(items[i], items[j], reg);
            if (g + est < best_cost)
                candidates.push_back({i, j, est});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        // Equipment-base (i==0) first — builds complete solutions fast
        if ((a.i == 0) != (b.i == 0))
            return a.i == 0;
        return a.est < b.est;
    });

    for (const auto &cand : candidates) {
        Item forged       = items[cand.i];
        int32_t real_cost = forge_engine.forge_into(forged, items[cand.j], reg);
        int32_t child_g   = g + real_cost;
        if (child_g >= best_cost)
            continue;

        std::vector<Item> child = items;
        child.erase(child.begin() + static_cast<std::ptrdiff_t>(cand.j));
        size_t base_in_child = (cand.i > cand.j) ? cand.i - 1 : cand.i;
        child[base_in_child] = std::move(forged);

        best_cost = dfs_bound(
            std::move(child), child_g, best_cost, node_limit, forge_engine, reg, target, h_buf, h_dirty
        );
        if (node_limit <= 0)
            return best_cost;
    }

    return best_cost;
}

} // namespace search_utils
} // namespace algorithm
