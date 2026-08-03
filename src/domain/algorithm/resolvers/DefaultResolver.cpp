#include "domain/algorithm/resolvers/DefaultResolver.h"
#include "domain/algorithm/resolvers/ItemResolver.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "common/utils/bit_iterator.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace algorithm {
namespace {

// ─── Cost / selection helpers (file-local) ────────────────────────────────
//
// Anvil reference: docs/mc/anvil-mechanics-reference.md
//   - Cost = P_A + P_B + C_ench;  C_ench = final_level × mult (Java)
//     (mult = reg[id].mul for equipment, reg[id].mul_b for books).
//   - Prior-work penalty: P = 2^n − 1 where n is the item's ppn field.
//   - Level merge: a == b ? min(max_lvl, a+1) : max(a, b).

/// Prior-work penalty value for an item whose ppn field is the exponent n.
/// Matches ForgeEngine::penalty_cost: ppn > 30 is beyond the vanilla anvil
/// model and treated as infeasible (INT32_MAX), so a ppn-31..59 pool item is
/// never scored as a cheap, valid base.
int64_t ppn_penalty(uint8_t ppn) {
    if (ppn > 30)
        return INT32_MAX;
    return (int64_t{1} << ppn) - 1;
}

/// Rough relative cost of using equipment \p e as the forge base for \p target
/// (used only to rank base candidates — approximation is fine):
///   penalty(e) + Σ over the enchant gap (target[id] − e[id]) of book costs.
int64_t est_forge_cost(const Item &e, const Item &target, const EnchReg &reg) {
    int64_t book_est = 0;
    bit_iterator<EnchSet::mask_type, uint8_t> it(target.enchs.get_mask());
    for (auto id = it.next(); id != it.npos; id = it.next()) {
        if (e.enchs[id] < target.enchs[id])
            book_est += static_cast<int64_t>(target.enchs[id] - e.enchs[id]) * reg[id].mul_b;
    }
    return ppn_penalty(e.ppn) + book_est;
}

/// Highest level reachable from the given book levels, combined via the anvil
/// rule (a == b → min(max_lvl, a+1), else max(a, b)).
///
/// Exact (never over-approximate): raising a level is only possible by merging
/// two EQUAL levels, so the greedy pair-carry below enumerates every real
/// merge.  Because it never reports a level the merge tree cannot produce, it
/// is conservative in the direction the selection needs — callers that stop
/// accumulating books once can_reach() returns true never under-keep.
uint8_t max_reachable_level(const std::vector<uint8_t> &levels, uint8_t max_lvl) {
    std::vector<int> freq(static_cast<size_t>(max_lvl) + 1, 0);
    for (uint8_t lv : levels) {
        uint8_t capped = lv < max_lvl ? lv : max_lvl;
        ++freq[capped];
    }
    uint8_t top = 0;
    for (int lv = 0; lv <= static_cast<int>(max_lvl); ++lv) {
        if (freq[lv] > 0)
            top = static_cast<uint8_t>(lv);
        if (lv < static_cast<int>(max_lvl)) {
            int pairs = freq[lv] / 2;
            if (pairs > 0)
                freq[lv + 1] += pairs;
        }
    }
    return top;
}

/// Can the given book levels, combined via the anvil rule, reach level
/// \p threshold?  Equivalent to max_reachable_level(levels) >= threshold.
bool can_reach(const std::vector<uint8_t> &levels, uint8_t threshold, uint8_t max_lvl) {
    if (threshold == 0)
        return true;  // a level-1+ book already reaches a zero gap
    return max_reachable_level(levels, max_lvl) >= threshold;
}

/// Whether \p e carries any enchant that conflicts with enchant \p id.
/// Uses the compact registry's conflict matrix (EnchReg::is_conflict).
bool carries_conflict_with(uint8_t id, const Item &e, const EnchReg &reg) {
    bit_iterator<EnchSet::mask_type, uint8_t> it(e.enchs.get_mask());
    for (auto id_c = it.next(); id_c != it.npos; id_c = it.next())
        if (reg.is_conflict(id_c, id))
            return true;
    return false;
}

/// Whether base \p e is infeasible: it carries an enchant that conflicts with a
/// target enchant still to be added (a gap).  The conflicting target enchant
/// can never be transferred onto \p e, so such a base cannot reach the target
/// and must not be selected as the forge base.
bool base_conflicts_with_gap(const Item &e, const Item &target, const EnchReg &reg) {
    bit_iterator<EnchSet::mask_type, uint8_t> it(target.enchs.get_mask());
    for (auto id_t = it.next(); id_t != it.npos; id_t = it.next()) {
        if (e.enchs[id_t] >= target.enchs[id_t])
            continue;  // already satisfied — no gap for this enchant
        if (carries_conflict_with(id_t, e, reg))
            return true;
    }
    return false;
}

/// Whether equipment \p k carries a target enchantment at a level the books in
/// \p books (combined via the anvil rule) CANNOT reach.  This covers both
/// bookless enchants (no book carries it at all → books reach 0) and the
/// under-level case (a thorns-1 book cannot reach a thorns-3 target even though
/// a thorns-3 equipment is present).  Such an enchant can only be supplied by
/// equipment, so \p k must be retained — provided the chosen \p base can
/// actually receive it (a base already carrying a conflicting enchant would
/// drop it during the merge, so it is not credited for that enchant).
bool carries_irreplaceable_target_ench(const Item &k, const Item &target,
                                       const std::vector<const Item *> &books,
                                       const EnchReg &reg, const Item &base) {
    bit_iterator<EnchSet::mask_type, uint8_t> it(target.enchs.get_mask());
    for (auto id = it.next(); id != it.npos; id = it.next()) {
        if (k.enchs[id] == 0)
            continue;
        if (carries_conflict_with(id, base, reg))
            continue;  // base would drop id during the merge — not helpful
        std::vector<uint8_t> book_levels;
        for (const Item *b : books)
            if (b->enchs[id] > 0)
                book_levels.push_back(b->enchs[id]);
        if (max_reachable_level(book_levels, reg[id].max_lvl) < target.enchs[id])
            return true;
    }
    return false;
}

/// acc[id] = max(acc[id], src[id]) for every enchant in \p src.
void merge_max(EnchSet &acc, const EnchSet &src) {
    bit_iterator<EnchSet::mask_type, uint8_t> it(src.get_mask());
    for (auto id = it.next(); id != it.npos; id = it.next()) {
        if (src[id] > acc[id])
            acc.insert(id, src[id]);
    }
}

/// Highest level carried by \p item (used as the sort key for multi-enchant
/// books whose per-id level is ambiguous in the final ordering).
uint8_t max_book_level(const Item &item) {
    uint8_t m = 0;
    bit_iterator<EnchSet::mask_type, uint8_t> it(item.enchs.get_mask());
    for (auto id = it.next(); id != it.npos; id = it.next())
        if (item.enchs[id] > m)
            m = item.enchs[id];
    return m;
}

/// Actual anvil cost of applying \p book's enchant \p id to a base already at
/// level \p base_level (Java): penalty(book) + final_level × mul_b, where
/// final_level follows the merge rule (equal → +1 capped at max_lvl, else max).
int64_t book_apply_cost(const Item &book, uint8_t id, uint8_t base_level,
                        const EnchReg &reg) {
    const uint8_t L   = book.enchs[id];
    const uint8_t fin = (L == base_level)
        ? std::min<uint8_t>(reg[id].max_lvl, static_cast<uint8_t>(base_level + 1))
        : std::max<uint8_t>(base_level, L);
    return ppn_penalty(book.ppn) + static_cast<int64_t>(fin) * reg[id].mul_b;
}

/// Diff-aware book selection: keep the books needed to fill the enchant gap
/// between \p effective (levels already provided by kept equipment) and
/// \p target.  Irrelevant books (no target-enchant overlap) and redundant
/// books (dominated by a sufficient one) are dropped; a book is retained when
/// selected for ANY target enchantment.
std::vector<const Item *> select_books(const std::vector<const Item *> &books,
                                       const EnchSet &effective, const Item &target,
                                       const EnchReg &reg) {
    std::vector<char> keep(books.size(), 0);
    bit_iterator<EnchSet::mask_type, uint8_t> it(target.enchs.get_mask());
    for (auto id = it.next(); id != it.npos; id = it.next()) {
        const uint8_t T = target.enchs[id];
        const uint8_t B = effective[id];
        if (B >= T)
            continue;  // gap already covered by equipment

        std::vector<size_t> cand;
        for (size_t i = 0; i < books.size(); ++i)
            if (books[i]->enchs[id] > 0)
                cand.push_back(i);
        if (cand.empty())
            continue;

        // Candidates: level desc, then ppn asc.  Priority lives only in the
        // business InventoryPayload and is consumed by the initial sort; the
        // compact Item carries no priority, so level/ppn are the sole tie-breaks.
        std::stable_sort(cand.begin(), cand.end(), [&](size_t a, size_t b) {
            if (books[a]->enchs[id] != books[b]->enchs[id])
                return books[a]->enchs[id] > books[b]->enchs[id];
            return books[a]->ppn < books[b]->ppn;
        });

        // Smallest book level that suffices when applied to base level B
        // (B < T): applying level M to base B yields
        //   M == B → B+1 (suffices when T == B+1),  M > B → M (suffices when
        //   M ≥ T),  M < B → B < T (never suffices).
        const uint8_t max_lvl   = reg[id].max_lvl;
        const uint8_t threshold = (T == static_cast<uint8_t>(B + 1)) ? B : T;

        // Phase A: a single book suffices → keep the sufficient book with the
        // lowest actual apply cost (penalty + final_level × mul_b), so a
        // low-ppn higher-level book beats a high-ppn lower-level one when both
        // reach the target; ties → lower level, then lower ppn.
        size_t single_idx = SIZE_MAX;
        int64_t single_cost = INT64_MAX;
        uint8_t single_level = 0, single_ppn = 0;
        for (size_t ci : cand) {
            const Item *c = books[ci];
            if (c->enchs[id] >= threshold) {
                const int64_t cost = book_apply_cost(*c, id, B, reg);
                if (single_idx == SIZE_MAX || cost < single_cost ||
                    (cost == single_cost && c->enchs[id] < single_level) ||
                    (cost == single_cost && c->enchs[id] == single_level &&
                     c->ppn < single_ppn)) {
                    single_idx   = ci;
                    single_cost  = cost;
                    single_level = c->enchs[id];
                    single_ppn   = c->ppn;
                }
            }
        }
        if (single_idx != SIZE_MAX) {
            keep[single_idx] = 1;
            continue;
        }

        // Phase B: no single book suffices — accumulate candidates (highest
        // level first, fewest books) until the merged set reaches the
        // threshold.  Conservative: never stops early, so it never under-keeps.
        std::vector<uint8_t> chosen;
        chosen.reserve(cand.size());
        for (size_t ci : cand) {
            chosen.push_back(books[ci]->enchs[id]);
            keep[ci] = 1;
            if (can_reach(chosen, threshold, max_lvl))
                break;
        }
    }

    std::vector<const Item *> out;
    for (size_t i = 0; i < books.size(); ++i)
        if (keep[i])
            out.push_back(books[i]);
    return out;
}

} // namespace

ResolverOutput DefaultResolver::resolve(const AlgorithmInput &input) const {
    switch (input.config.mode) {
    case AlgorithmMode::direct: {
        const auto *d = std::get_if<DirectPayload>(&input.data);
        if (!d)
            return {};
        // Base equipment = target with the source (current) enchantments.
        Item base = input.target;
        base.enchs.clear();
        for (const Ench &e : d->source)
            base.enchs.insert(e);
        // Generate the books needed to reach the target from the source.
        ResolverOutput books = ItemResolver::resolve(input.target, base.enchs);
        ResolverOutput out;
        // A book target with no source has no base book to preserve: its first
        // enchantment comes from the enchanting table (book → enchanted_book),
        // outside the anvil model.  Emitting an empty base book would force a
        // pointless merge step that only inflates the final ppn (and its cost).
        out.reserve(books.size() + (base.enchs.empty() ? 0 : 1));
        if (!(input.target.type == ItemType::Book && base.enchs.empty()))
            out.push_back(std::move(base));  // equipment base / sourced book
        for (auto &b : books)
            out.push_back(std::move(b));
        return out;
    }
    case AlgorithmMode::inventory: {
        const auto *inv = std::get_if<InventoryPayload>(&input.data);
        if (!inv || inv->available.empty())
            return {};

        // Priority is a business-domain attribute
        // (InventoryPayload.extra_item_priorities) that is consumed here for
        // the initial global ordering only — the compact Item carries no
        // priority, so every later sort key is (level, ppn).
        struct RankedItem {
            const Item *item;
            int32_t priority;
        };
        std::vector<RankedItem> ranked;
        ranked.reserve(inv->available.size());
        for (size_t i = 0; i < inv->available.size(); ++i) {
            int32_t prio = (i < inv->priorities.size()) ? inv->priorities[i] : 99;
            ranked.push_back({&inv->available[i], prio});
        }
        std::stable_sort(ranked.begin(), ranked.end(),
            [](const RankedItem &a, const RankedItem &b) {
                return a.priority < b.priority;
            });

        const EnchReg &reg = input.registry;
        const Item &target = input.target;

        // Partition the priority-ordered pool into books / equipment.
        std::vector<const Item *> books, equips;
        books.reserve(ranked.size());
        equips.reserve(ranked.size());
        for (const auto &r : ranked) {
            if (r.item->type == ItemType::Equip)
                equips.push_back(r.item);
            else if (r.item->type == ItemType::Book)
                books.push_back(r.item);
        }

        if (target.type == ItemType::Equip) {
            // A pure-book pool can never produce an equipment target (books
            // cannot be forged into equipment).  Regression-preserving: the
            // former resolver rejected this configuration too.
            if (equips.empty())
                return {};

            // Phase 2 — best base: the feasible equipment minimizing
            // est_forge_cost.  A base carrying an enchant that conflicts with a
            // still-needed target enchant is infeasible (that target enchant
            // can never be added to it) and is skipped.
            const Item *best = nullptr;
            for (const Item *e : equips) {
                if (base_conflicts_with_gap(*e, target, reg))
                    continue;
                if (best == nullptr ||
                    est_forge_cost(*e, target, reg) < est_forge_cost(*best, target, reg))
                    best = e;
            }
            if (best == nullptr)
                return {};  // every equipment conflicts with a needed enchant

            // Phase 3 — retain equipment carrying a target enchant that the
            // books cannot reach (a book can never supply it) and the base can
            // actually receive.
            std::vector<const Item *> kept_equips;
            kept_equips.reserve(equips.size());
            kept_equips.push_back(best);
            for (const Item *k : equips) {
                if (k == best)
                    continue;
                if (carries_irreplaceable_target_ench(*k, target, books, reg, *best))
                    kept_equips.push_back(k);
            }

            // Phase 4 — diff-aware book selection against the kept-equipment
            // effective levels (gap = target − effective).
            EnchSet effective;
            for (const Item *e : kept_equips)
                merge_max(effective, e->enchs);
            std::vector<const Item *> selected = select_books(books, effective, target, reg);

            // Phase 5 — order: equipment first (best base first), then books
            // by (level desc, ppn asc); stable so equal (level, ppn) books keep
            // their priority order from the initial pool sort.
            std::stable_sort(selected.begin(), selected.end(),
                [](const Item *a, const Item *b) {
                    uint8_t la = max_book_level(*a), lb = max_book_level(*b);
                    if (la != lb)
                        return la > lb;
                    return a->ppn < b->ppn;
                });

            ResolverOutput out;
            out.reserve(kept_equips.size() + selected.size());
            for (const Item *e : kept_equips)
                out.push_back(*e);
            for (const Item *b : selected)
                out.push_back(*b);
            return out;
        }

        // target.type == Book — a pure-book pool IS reachable (books forge
        // together to produce the target book; no equipment base needed).  Only
        // relevance + diff filtering applies; the strategy picks the base book.
        std::vector<const Item *> selected = select_books(books, EnchSet{}, target, reg);
        std::stable_sort(selected.begin(), selected.end(),
            [](const Item *a, const Item *b) {
                uint8_t la = max_book_level(*a), lb = max_book_level(*b);
                if (la != lb)
                    return la > lb;
                return a->ppn < b->ppn;
            });

        ResolverOutput out;
        out.reserve(selected.size());
        for (const Item *b : selected)
            out.push_back(*b);
        return out;
    }
    }
    return {};
}

} // namespace algorithm
