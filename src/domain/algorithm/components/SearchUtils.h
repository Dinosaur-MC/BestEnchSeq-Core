#pragma once
#include "ItemPool.h"
#include "common/utils/bit_iterator.hpp"
#include "domain/algorithm/forge_engine/IForgeEngine.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Item.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace algorithm {

// ─── Shared helpers ───────────────────────────────────────────────────

/// Check whether \p item satisfies the full \p target: it must be the same
/// item type (equipment for gear targets, a book for enchanted-book targets —
/// a book result can never satisfy an equipment target, and vice versa) and
/// carry every target enchantment at or above the required level.
inline bool meets_target(const Item &item, const Item &target) noexcept {
    if (item.type != target.type)
        return false;
    // Traverse the target's enchanted-id mask with the low-overhead bit
    // iterator rather than the EnchSet input iterator (hot path: final-item
    // checks in every strategy run per candidate).
    bit_iterator<EnchSet::mask_type, uint8_t> it(target.enchs.get_mask());
    for (auto id = it.next(); id != it.npos; id = it.next()) {
        if (item.enchs[id] < target.enchs[id])
            return false;
    }
    return true;
}

/// Pool-based overload: resolves \p equip_id through \p pool.
inline bool meets_target(ItemPool::ItemID equip_id, const ItemPool &pool, const Item &target) noexcept {
    return meets_target(pool[equip_id], target);
}

/// Move the first equipment item to the front of a working item set, so
/// items[0] is a sane base for strategies that rely on index-0-as-equipment.
/// No-op when there is no equipment (direct mode already places it first;
/// a book-only pool stays in priority order).
inline void normalize_base_equipment(ItemCollection &items) noexcept {
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].type == ItemType::Equip) {
            if (i != 0)
                std::rotate(items.begin(), items.begin() + i, items.begin() + i + 1);
            return;
        }
    }
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
                    yield(e.id(), e.level());
        },
        reg, h_max, h_dirty
    );
}

// ─── Compute heuristic h from precomputed max levels ──────────────────

template <typename EnchRange>
inline int32_t
compute_h(const EnchRange &target, const EnchReg &reg, const std::vector<int16_t> &h_max) {
    int32_t h = 0;
    for (const auto &t : target) {
        Ench e  = static_cast<Ench>(t);
        int16_t have = h_max[e.id];
        if (have < e.level)
            h += (e.level - have) * reg[e.id].mul_b;
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
    const IForgeEngine &forge_engine, const EnchReg &reg, const Item &target,
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
                    if (e.id() >= 0)
                        yield(e.id(), e.level());
        },
        reg, h_buf, h_dirty
    );
    int32_t h = search_utils::compute_h(target.enchs, reg, h_buf);
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
