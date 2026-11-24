#include "WeightClassDp.h"
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <map>
#include <string>
#include <tuple>

namespace algorithm {

namespace {

constexpr int kMaxH = 32;      // result heights 0..31
constexpr int kMaxChildH = 30; // ForgeEngine::penalty_cost is finite for ppn ≤ 30
constexpr int64_t kInf = std::numeric_limits<int64_t>::max() / 4;
constexpr uint64_t kMaxStates = uint64_t{1} << 24;

/// One Pareto frontier entry: the cheapest way to reach `h` for a state, plus
/// the split that produced it (for reconstruction).
struct Entry {
    int64_t cost;
    uint32_t sub; // base part of the split (F: book sub-multiset, G: base-side sub-multiset)
    uint8_t h;    // resulting height (= ppn)
    uint8_t ia;   // entry index in the base part's frontier
    uint8_t ib;   // entry index in the sacrifice part's frontier
};

struct Span {
    uint32_t off = 0;
    uint8_t n = 0;
};

/// Books grouped into interchangeable weight classes + per-state tables.
struct Classes {
    int d = 0;
    std::vector<int> cnt;
    std::vector<int64_t> wb, we;              // per class: book-side / base-side weight
    std::vector<uint8_t> ppn;                 // per class: leaf ppn
    std::vector<std::vector<size_t>> members; // per class: indices into items
    std::vector<uint64_t> stride;             // mixed-radix strides (d + 1)
    uint64_t N = 1;                           // number of sub-multisets
    std::vector<int64_t> Vb, Ve;              // per state: Σ w_book / Σ w_base
    std::vector<uint16_t> size;               // per state: number of books
};

/// F[s]: books of multiset s merged into one book.  G[s]: base + books of s.
struct Pass {
    std::vector<Entry> pool;
    std::vector<Span> F, G;
    uint64_t cap_pruned = 0;
    uint64_t subproblems = 0;
    uint32_t max_frontier = 0;
    bool cancelled = false;
};

uint64_t sat_mul(uint64_t a, uint64_t b) noexcept {
    if (a != 0 && b > std::numeric_limits<uint64_t>::max() / a)
        return std::numeric_limits<uint64_t>::max();
    return a * b;
}

template <typename Pen>
bool build_classes(
    const ItemCollection& items, const Item& target, const EnchReg& reg, const ForgeEngine& engine, Pen&& pen, Classes& c) {
    if (items.size() < 2 || items[0].type != ItemType::Equip)
        return false;
    const Item& base = items[0];
    if (base.ppn > kMaxChildH)
        return false;

    // Books only, each non-empty, enchantments pairwise disjoint (a shared
    // enchant means level-up merges — not additive, general search owns it).
    // Every book must also be necessary — it carries a target enchant the base
    // still lacks, which (books being disjoint) no other book can supply — so
    // "merge everything" is the only plan shape.  Direct mode always satisfies
    // this; an inventory pool with spare books goes to the general search,
    // which picks the cheapest subset.
    EnchSet::mask_type books_mask = 0;
    for (size_t i = 1; i < items.size(); ++i) {
        const Item& b = items[i];
        if (b.type != ItemType::Book || b.enchs.empty() || b.ppn > kMaxChildH)
            return false;
        const auto m = b.enchs.get_mask();
        if (m & books_mask)
            return false;
        books_mask |= m;
        bool needed = false;
        for (auto t = m & target.enchs.get_mask(); t && !needed; t &= t - 1) {
            const auto id = static_cast<EnchReg::id_type>(std::countr_zero(t));
            needed = base.enchs[id] < target.enchs[id];
        }
        if (!needed)
            return false;
    }

    // No book enchant may conflict with another book's or the base's (forge_into
    // would drop it), and every enchant new to the base must be applicable.
    const auto& cfg = engine.get_config();
    const auto all = books_mask | base.enchs.get_mask();
    for (auto m = books_mask; m; m &= m - 1) {
        const auto id = static_cast<EnchReg::id_type>(std::countr_zero(m));
        const auto self = EnchSet::mask_type{1} << id;
        if (!cfg.ignore_imcompatible && (all & reg.get_conflict_mask(id) & ~self))
            return false;
        if (!base.enchs.contains(id) && !reg.is_applicable(id))
            return false;
    }

    // Per-book weights, measured through the engine itself.
    const Item empty_book{ItemType::Book, 0, 0, {}};
    Item base0 = base;
    base0.ppn = 0;
    std::map<std::tuple<int64_t, int64_t, uint8_t>, size_t> index;
    std::vector<std::tuple<int64_t, int64_t, uint8_t>> keys(items.size());
    for (size_t i = 1; i < items.size(); ++i) {
        const Item& b = items[i];
        const int32_t c1 = engine.forge(empty_book, b, reg).second;
        const int32_t c2 = engine.forge(base0, b, reg).second;
        if (c1 == INT32_MAX || c2 == INT32_MAX)
            return false;
        const int64_t wb = c1 - pen(0) - pen(b.ppn);
        const int64_t we = c2 - pen(0) - pen(b.ppn);
        if (wb < 0 || we < 0)
            return false;
        keys[i] = {wb, we, b.ppn};
        index.emplace(keys[i], 0);
    }
    c.d = static_cast<int>(index.size());
    size_t next = 0;
    for (auto& [key, idx] : index) {
        idx = next++;
        c.wb.push_back(std::get<0>(key));
        c.we.push_back(std::get<1>(key));
        c.ppn.push_back(std::get<2>(key));
    }
    c.cnt.assign(c.d, 0);
    c.members.assign(c.d, {});
    for (size_t i = 1; i < items.size(); ++i) {
        const size_t j = index[keys[i]];
        ++c.cnt[j];
        c.members[j].push_back(i);
    }
    return true;
}

/// Π(c_j + 1) states and Π(c_j + 1)(c_j + 2)/2 splits per DP table.
bool within_budget(Classes& c, uint64_t split_budget) {
    c.stride.assign(c.d + 1, 1);
    uint64_t splits = 1;
    for (int j = 0; j < c.d; ++j) {
        const auto cj = static_cast<uint64_t>(c.cnt[j]);
        c.stride[j + 1] = sat_mul(c.stride[j], cj + 1);
        splits = sat_mul(splits, (cj + 1) * (cj + 2) / 2);
    }
    c.N = c.stride[c.d];
    return c.N <= kMaxStates && sat_mul(splits, 2) <= split_budget;
}

void build_state_tables(Classes& c) {
    c.Vb.assign(c.N, 0);
    c.Ve.assign(c.N, 0);
    c.size.assign(c.N, 0);
    std::vector<int> dg(c.d, 0);
    int64_t vb = 0, ve = 0;
    int sz = 0;
    for (uint64_t s = 1; s < c.N; ++s) {
        // odometer increment of the digit vector, keeping the sums in sync
        for (int j = 0; j < c.d; ++j) {
            if (dg[j] < c.cnt[j]) {
                ++dg[j];
                vb += c.wb[j];
                ve += c.we[j];
                ++sz;
                break;
            }
            vb -= dg[j] * c.wb[j];
            ve -= dg[j] * c.we[j];
            sz -= dg[j];
            dg[j] = 0;
        }
        c.Vb[s] = vb;
        c.Ve[s] = ve;
        c.size[s] = static_cast<uint16_t>(sz);
    }
}

template <typename Pen> Pass run_pass(const Classes& c, uint8_t base_ppn, int32_t cap, Pen&& pen, ExecutionContext& ctx) {
    Pass p;
    p.F.assign(c.N, {});
    p.G.assign(c.N, {});
    p.pool.reserve(static_cast<size_t>(c.N) * 3);
    auto seed = [&](Span& sp, uint8_t h) {
        sp.off = static_cast<uint32_t>(p.pool.size());
        sp.n = 1;
        p.pool.push_back(Entry{0, 0, h, 0, 0});
    };
    for (int j = 0; j < c.d; ++j)
        seed(p.F[c.stride[j]], c.ppn[j]);
    seed(p.G[0], base_ppn);

    std::array<int64_t, kMaxH> best{};
    std::array<uint32_t, kMaxH> bsub{};
    std::array<uint8_t, kMaxH> bia{}, bib{};

    auto combine = [&](const Span& X, const Span& Y, int64_t v, uint32_t sub) {
        for (uint8_t a = 0; a < X.n; ++a) {
            const Entry& ex = p.pool[X.off + a];
            if (ex.h > kMaxChildH)
                continue;
            for (uint8_t b = 0; b < Y.n; ++b) {
                const Entry& ey = p.pool[Y.off + b];
                if (ey.h > kMaxChildH)
                    continue;
                const int64_t step = pen(ex.h) + pen(ey.h) + v;
                if (cap > 0 && step > cap) {
                    ++p.cap_pruned;
                    continue;
                }
                const int hh = std::max(ex.h, ey.h) + 1;
                const int64_t tot = ex.cost + ey.cost + step;
                if (tot < best[hh]) { // strict: first split found wins → deterministic
                    best[hh] = tot;
                    bsub[hh] = sub;
                    bia[hh] = a;
                    bib[hh] = b;
                }
            }
        }
    };
    // Keep height h only if strictly cheaper than every lower height: a taller
    // result costs more in every later forge, so it must pay for itself now.
    auto finish = [&](Span& out) {
        out.off = static_cast<uint32_t>(p.pool.size());
        out.n = 0;
        int64_t run = kInf;
        for (int hh = 0; hh < kMaxH; ++hh)
            if (best[hh] < run) {
                run = best[hh];
                p.pool.push_back(Entry{best[hh], bsub[hh], static_cast<uint8_t>(hh), bia[hh], bib[hh]});
                ++out.n;
            }
        if (out.n) {
            ++p.subproblems;
            p.max_frontier = std::max<uint32_t>(p.max_frontier, out.n);
        }
    };

    std::vector<int> ds(c.d), a(c.d);
    auto for_each_sub = [&](auto&& fn) { // all sub-vectors a ≤ ds (mixed-radix odometer)
        std::fill(a.begin(), a.end(), 0);
        uint64_t ai = 0;
        for (;;) {
            fn(ai);
            int j = 0;
            for (; j < c.d; ++j) {
                if (a[j] < ds[j]) {
                    ++a[j];
                    ai += c.stride[j];
                    break;
                }
                ai -= static_cast<uint64_t>(a[j]) * c.stride[j];
                a[j] = 0;
            }
            if (j == c.d)
                break;
        }
    };

    for (uint64_t s = 1; s < c.N; ++s) {
        if ((s & 1023) == 0) {
            ctx.wait_if_paused();
            if (ctx.is_cancelled()) {
                p.cancelled = true;
                return p;
            }
        }
        uint64_t r = s;
        for (int j = 0; j < c.d; ++j) {
            ds[j] = static_cast<int>(r % static_cast<uint64_t>(c.cnt[j] + 1));
            r /= static_cast<uint64_t>(c.cnt[j] + 1);
        }
        if (c.size[s] >= 2) {
            best.fill(kInf);
            for_each_sub([&](uint64_t A) {
                if (A == 0 || A == s)
                    return;
                const uint64_t B = s - A;
                const Span& fa = p.F[A];
                const Span& fb = p.F[B];
                if (fa.n && fb.n)
                    combine(fa, fb, c.Vb[B], static_cast<uint32_t>(A));
            });
            finish(p.F[s]);
        }
        best.fill(kInf);
        for_each_sub([&](uint64_t T) {
            if (T == s)
                return;
            const uint64_t B = s - T;
            const Span& gt = p.G[T];
            const Span& fb = p.F[B];
            if (gt.n && fb.n)
                combine(gt, fb, c.Ve[B], static_cast<uint32_t>(T));
        });
        finish(p.G[s]);
    }
    return p;
}

/// Rebuild the chosen tree and replay every forge through the real engine.
struct Replay {
    const Classes& c;
    const Pass& p;
    const ItemCollection& items;
    const EnchReg& reg;
    const ForgeEngine& engine;
    std::vector<size_t> cursor;
    std::vector<EnchStep> steps;
    int64_t total = 0;
    int32_t max_step = 0;

    Item forge(Item base, const Item& sac) {
        Item res = base;
        const int32_t cost = engine.forge_into(res, sac, reg);
        total += cost;
        max_step = std::max(max_step, cost);
        steps.push_back(EnchStep{std::move(base), sac, res, cost});
        return res;
    }
    // Post-order (base subtree, sacrifice subtree, this forge) — the order
    // StepTree::materialize() produces for the general DPs.
    Item emit_f(uint64_t s, uint8_t idx) {
        if (c.size[s] == 1) {
            int j = 0;
            while (c.stride[j] != s)
                ++j;
            return items[c.members[j][cursor[j]++]];
        }
        const Entry& e = p.pool[p.F[s].off + idx];
        Item base = emit_f(e.sub, e.ia);
        Item sac = emit_f(s - e.sub, e.ib);
        return forge(std::move(base), sac);
    }
    Item emit_g(uint64_t s, uint8_t idx) {
        if (s == 0)
            return items[0];
        const Entry& e = p.pool[p.G[s].off + idx];
        Item base = emit_g(e.sub, e.ia);
        Item sac = emit_f(s - e.sub, e.ib);
        return forge(std::move(base), sac);
    }
};

} // namespace

bool WeightClassDp::enabled(const SearchConfig& cfg, std::string_view strategy) {
    std::string key(strategy);
    key += ".weight_class";
    const auto it = cfg.extra.find(key);
    if (it == cfg.extra.end())
        return true;
    const auto& v = it->second;
    return !(v == "off" || v == "0" || v == "false");
}

WeightClassDp::Result WeightClassDp::solve(const ItemCollection& items,
                                           const Item& target,
                                           const EnchReg& reg,
                                           const ForgeEngine& engine,
                                           const Options& opt,
                                           ExecutionContext& ctx) {
    Result r;
    const auto& cfg = engine.get_config();
    auto pen = [&](int h) -> int64_t {
        return cfg.ignore_penalty_cost ? 0 : static_cast<int64_t>(engine.penalty_cost(static_cast<int8_t>(h)));
    };

    Classes c;
    if (!build_classes(items, target, reg, engine, pen, c) || !within_budget(c, opt.split_budget))
        return r;
    build_state_tables(c);
    r.classes = static_cast<uint32_t>(c.d);
    r.states = c.N;

    const int32_t cap = opt.max_step_cost > 0 ? opt.max_step_cost : 0;

    struct Plan {
        bool found = false;
        int64_t cost = 0;
        int32_t max_step = 0;
        std::vector<EnchStep> steps;
    };
    bool mismatch = false;
    // Returns false when cancelled.
    auto attempt = [&](int32_t pass_cap, Plan& out) -> bool {
        Pass p = run_pass(c, items[0].ppn, pass_cap, pen, ctx);
        r.cap_pruned += p.cap_pruned;
        r.subproblems = p.subproblems;
        r.max_frontier = p.max_frontier;
        if (p.cancelled)
            return false;
        out.found = false;
        const Span& g = p.G[c.N - 1];
        if (g.n == 0)
            return true;
        // Frontier costs strictly decrease with height → the cheapest is last.
        const uint8_t idx = static_cast<uint8_t>(g.n - 1);
        const int64_t dp_cost = p.pool[g.off + idx].cost;
        Replay rp{c, p, items, reg, engine, std::vector<size_t>(c.d, 0), {}, 0, 0};
        const Item fin = rp.emit_g(c.N - 1, idx);
        if (rp.total != dp_cost || !meets_target(fin, target)) {
            mismatch = true;
            return true;
        }
        out.found = true;
        out.cost = dp_cost;
        out.max_step = rp.max_step;
        out.steps = std::move(rp.steps);
        return true;
    };

    Plan best;
    if (opt.objective == Objective::CapFirst) {
        if (cap > 0) {
            if (!attempt(cap, best)) {
                r.status = Status::Cancelled;
                return r;
            }
            if (mismatch)
                return r;
        }
        if (!best.found) {
            if (!attempt(0, best)) {
                r.status = Status::Cancelled;
                return r;
            }
            if (mismatch)
                return r;
            r.relaxed = (cap > 0);
        }
    } else {
        if (!attempt(0, best)) {
            r.status = Status::Cancelled;
            return r;
        }
        if (mismatch)
            return r;
        if (best.found && cap > 0 && best.max_step > cap) {
            Plan capped;
            if (!attempt(cap, capped)) {
                r.status = Status::Cancelled;
                return r;
            }
            if (mismatch)
                return r;
            if (capped.found && capped.cost == best.cost)
                best = std::move(capped);
        }
    }
    if (!best.found)
        return r; // let the general search decide

    r.status = Status::Solved;
    r.cost = best.cost;
    r.steps = std::move(best.steps);
    return r;
}

} // namespace algorithm
