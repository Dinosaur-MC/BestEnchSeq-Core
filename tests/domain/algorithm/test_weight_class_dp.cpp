// =============================================================================
// WeightClassDp (direct-mode fast path of dp_merge / bb_dp) tests.
//
//   * equivalence: on a deterministic random corpus (Java + Bedrock, sourced
//     equipment incl. same-level upgrades, conflicting non-target enchants),
//     the fast path reaches exactly the optimum the general partition DP finds
//     — for dp_merge (min cost) and bb_dp (≤cap first, then relax);
//   * plan validity: every reported step replays through ForgeEngine and the
//     steps form one merge tree over the resolver's items;
//   * dp_merge tie-break: whenever a ≤cap plan costs the optimum, dp_merge's
//     fast plan has every step ≤ cap;
//   * known optima (doc / benchmark values) and a 24-book case the general
//     search cannot finish;
//   * ineligible shapes and the opt-out switch fall back to the general search.
// =============================================================================

#define BESQ_TEST_MAIN
#include "domain/algorithm/_strategies/bb_dp/BBDpAlgorithm.h"
#include "domain/algorithm/_strategies/dp_merge/DPMergeAlgorithm.h"
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/components/WeightClassDp.h"
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "framework/test_framework.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace algorithm;

namespace {

struct Spec {
    int mul; // item multiplier (book multiplier = max(1, mul >> 1))
    int max_lvl;
};

EnchReg make_reg(const std::vector<Spec>& specs, const std::vector<std::pair<int, int>>& conflicts = {}) {
    std::vector<EnchInfo> infos(specs.size());
    std::vector<NSID> gids;
    Equipment eq;
    eq.id = NSID("test_sword");
    eq.max_durability = 1561;
    for (size_t i = 0; i < specs.size(); ++i) {
        infos[i].id = static_cast<uint8_t>(i);
        infos[i].mul = static_cast<uint8_t>(specs[i].mul);
        infos[i].mul_b = static_cast<uint8_t>(std::max(1, specs[i].mul >> 1));
        infos[i].max_lvl = static_cast<uint8_t>(specs[i].max_lvl);
        infos[i].exc_mask = 0;
        infos[i].applicable = true;
        eq.applicable_enchs.insert(static_cast<int16_t>(i));
        gids.push_back(NSID("e" + std::to_string(i)));
    }
    for (auto [a, b] : conflicts) {
        infos[a].exc_mask |= mask_type{1} << b;
        infos[b].exc_mask |= mask_type{1} << a;
    }
    EnchReg reg;
    reg.init(std::move(infos), std::move(gids), eq);
    return reg;
}

struct Task {
    EnchReg reg;
    Item target{ItemType::Equip, 1561, 0, {}};
    EnchCollection source;
    MCE platform = MCE::Java;
    int32_t cap = 39;
};

struct Outcome {
    bool solved = false;
    int32_t cost = -1;
    int32_t max_step = 0;
    uint32_t classes = 0; // dp_weight_classes (> 0 = fast path answered)
    std::vector<EnchStep> steps;
};

AlgorithmInput make_input(const Task& t, const char* strategy, bool fast) {
    AlgorithmInput in;
    in.config.forge.platform = t.platform;
    in.config.mode = AlgorithmMode::direct;
    in.config.search.max_step_cost = t.cap;
    if (!fast)
        in.config.search.extra[std::string(strategy) + ".weight_class"] = "off";
    in.registry = t.reg;
    in.target = t.target;
    in.data = DirectPayload{t.source};
    return in;
}

template <typename Algo> Outcome run(const Task& t, bool fast) {
    Algo algo;
    const char* name = algo.name().data();
    AlgorithmInput in = make_input(t, name, fast);
    ExecutionContext ctx(0, name);
    algo.init(in, ctx);
    algo.execute(in, ctx);
    Outcome o;
    auto sols = ctx.get_solutions();
    if (!sols.empty()) {
        o.solved = true;
        o.cost = sols[0].total_cost;
        o.steps = sols[0].steps;
        for (const auto& s : o.steps)
            o.max_step = std::max(o.max_step, s.cost);
    }
    if (auto d = ctx.consume_exit_diagnostics())
        if (auto* pd = dynamic_cast<PartitionDpDiagnostics*>(d.get()))
            o.classes = pd->dp_weight_classes;
    return o;
}

/// Every step replays through ForgeEngine, operands are leaves (ppn 0, the
/// resolver's items) or earlier results consumed once, and the plan ends in a
/// single item meeting the target.
void check_plan(const Task& t, const Outcome& o, size_t n_items, const std::string& label) {
    ForgeConfig cfg;
    cfg.platform = t.platform;
    ForgeEngine eng(cfg);
    std::vector<Item> produced;
    size_t leaves = 0;
    int32_t sum = 0;
    auto take = [&](const Item& it) {
        for (auto p = produced.begin(); p != produced.end(); ++p)
            if (*p == it) {
                produced.erase(p);
                return true;
            }
        if (it.ppn == 0) {
            ++leaves;
            return true;
        }
        return false;
    };
    for (const auto& s : o.steps) {
        Item r = s.base;
        const int32_t c = eng.forge_into(r, s.sacrifice, t.reg);
        expect(r == s.result && c == s.cost, "step replays through ForgeEngine: " + label);
        expect(take(s.base) && take(s.sacrifice), "step operands are leaves or unconsumed results: " + label);
        produced.push_back(s.result);
        sum += c;
    }
    expect_eq(sum, o.cost, "plan total equals sum of steps: " + label);
    expect(produced.size() == 1 && leaves == n_items, "plan is one merge tree over all items: " + label);
    expect(meets_target(o.steps.back().result, t.target), "plan meets the target: " + label);
}

Ench E(int id, int lvl) {
    return Ench{static_cast<Ench::value_type>(id), static_cast<Ench::value_type>(lvl)};
}

} // namespace

// ─── Equivalence on a random corpus ──────────────────────────────────────

TEST_CASE_TIMEOUT("test_weight_class_dp_equivalence", 300) {
    std::mt19937 rng(20261121);
    const int muls[] = {1, 2, 4, 8, 3, 6};
    int fast_hits = 0, trials = 0;
    for (int trial = 0; trial < 160; ++trial) {
        const int n_ench = 10;
        std::vector<Spec> specs;
        for (int i = 0; i < n_ench; ++i)
            specs.push_back({muls[rng() % 6], 1 + static_cast<int>(rng() % 5)});
        // enchant 9 conflicts with 8: 8 may be targeted, 9 only appears unused
        Task t;
        t.reg = make_reg(specs, {{8, 9}});
        t.platform = (trial % 3 == 2) ? MCE::Bedrock : MCE::Java;
        t.cap = (trial % 4 == 3) ? 0 : 39;
        const int k = 1 + static_cast<int>(rng() % 9); // 1..9 target enchants (ids 0..8)
        std::vector<int> ids = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        std::shuffle(ids.begin(), ids.end(), rng);
        for (int i = 0; i < k; ++i) {
            const int id = ids[i];
            const int lvl = specs[id].max_lvl;
            t.target.enchs.insert(E(id, lvl));
            const int roll = static_cast<int>(rng() % 6);
            if (roll == 0 && lvl >= 2)
                t.source.push_back(E(id, lvl - 1)); // same-level upgrade book
            else if (roll == 1 && lvl >= 3)
                t.source.push_back(E(id, 1)); // partial source level
        }
        const std::string label = "trial " + std::to_string(trial) + " k=" + std::to_string(k) +
                                  (t.platform == MCE::Bedrock ? " BE" : " JE") + " cap=" + std::to_string(t.cap);

        const Outcome dg = run<DPMergeAlgorithm>(t, false);
        const Outcome df = run<DPMergeAlgorithm>(t, true);
        const Outcome bg = run<BBDpAlgorithm>(t, false);
        const Outcome bf = run<BBDpAlgorithm>(t, true);
        ++trials;
        expect_eq(df.solved, dg.solved, "dp_merge fast/general agree on reachability: " + label);
        expect_eq(df.cost, dg.cost, "dp_merge fast == general optimum: " + label);
        expect_eq(bf.solved, bg.solved, "bb_dp fast/general agree on reachability: " + label);
        expect_eq(bf.cost, bg.cost, "bb_dp fast == general optimum: " + label);
        if (t.cap > 0)
            expect_eq(bf.max_step <= t.cap, bg.max_step <= t.cap, "bb_dp fast/general agree on cap feasibility: " + label);
        expect(df.classes > 0 && bf.classes > 0, "fast path answered the eligible shape: " + label);
        expect(dg.classes == 0 && bg.classes == 0, "opt-out ran the general search: " + label);
        if (df.solved && !df.steps.empty()) {
            // resolver items: base + one book per enchant still below target
            size_t n_items = 1;
            for (const auto& e : t.target.enchs) {
                bool done = false;
                for (const auto& s : t.source)
                    if (s.id == e.id() && s.level >= e.level())
                        done = true;
                if (!done)
                    ++n_items;
            }
            check_plan(t, df, n_items, label + " [dp_merge]");
            check_plan(t, bf, n_items, label + " [bb_dp]");
        }
        // dp_merge tie-break: a ≤cap plan at the optimum cost must be preferred.
        if (t.cap > 0 && bf.solved && bf.max_step <= t.cap && bf.cost == df.cost)
            expect(df.max_step <= t.cap, "dp_merge prefers a ≤cap plan at equal cost: " + label);
        if (df.classes > 0)
            ++fast_hits;
    }
    std::cout << "  corpus: " << trials << " trials, fast path answered " << fast_hits << std::endl;
    TEST_PASS("weight-class fast path == general partition DP (dp_merge + bb_dp)");
}

// ─── Known optima ─────────────────────────────────────────────────────────
//
// Java edition, bare equipment.  sword_combat_7 = 49 and netherite_boots = 88
// are the A*-verified optima of the 14-case benchmark
// (docs/algotithm_designs/{algorithm-design-discussion,hamming-algorithm-design}.md).

namespace {
Task bare_task(const std::vector<std::pair<int, int>>& mul_lvl) {
    std::vector<Spec> specs;
    for (auto [mul, lvl] : mul_lvl)
        specs.push_back({mul, lvl});
    Task t;
    t.reg = make_reg(specs);
    for (size_t i = 0; i < mul_lvl.size(); ++i)
        t.target.enchs.insert(E(static_cast<int>(i), mul_lvl[i].second));
    return t;
}
} // namespace

TEST_CASE("test_weight_class_dp_known_optima") {
    // sharpness 5, sweeping_edge 3, looting 3, unbreaking 3, fire_aspect 2, knockback 2, mending 1
    Task combat7 = bare_task({{1, 5}, {4, 3}, {4, 3}, {2, 3}, {4, 2}, {2, 2}, {4, 1}});
    const Outcome a = run<DPMergeAlgorithm>(combat7, true);
    expect(a.classes > 0, "sword_combat_7 answered by the fast path");
    expect_eq(a.cost, 49, "sword_combat_7 optimum = 49");

    // protection 4, feather_falling 4, depth_strider 3, soul_speed 3, thorns 3,
    // unbreaking 3, mending 1, vanishing_curse 1, binding_curse 1
    Task boots = bare_task({{1, 4}, {2, 4}, {4, 3}, {8, 3}, {8, 3}, {2, 3}, {4, 1}, {8, 1}, {8, 1}});
    const Outcome b = run<DPMergeAlgorithm>(boots, true);
    expect(b.classes > 0, "netherite_boots answered by the fast path");
    expect_eq(b.cost, 88, "netherite_boots optimum = 88");
    TEST_PASS("weight-class fast path reproduces known optima");
}

// ─── Scale: large.json sword_24 (the general DPs time out from 20 books) ──

TEST_CASE_TIMEOUT("test_weight_class_dp_scale", 120) {
    // data/tests/testcases/large.json sword_24 (vanilla + modded_sword.json multipliers)
    Task t = bare_task({{1, 5}, {4, 3}, {4, 3}, {2, 3}, {4, 2}, {2, 2}, {4, 1}, {8, 1}, {4, 2}, {3, 3}, {2, 3}, {4, 3},
                        {3, 2}, {4, 3}, {6, 1}, {2, 3}, {3, 2}, {4, 2}, {2, 3}, {5, 2}, {3, 2}, {3, 2}, {3, 3}, {2, 3}});
    t.cap = 39;
    const auto t0 = std::chrono::steady_clock::now();
    const Outcome d = run<DPMergeAlgorithm>(t, true);
    const auto t1 = std::chrono::steady_clock::now();
    const Outcome b = run<BBDpAlgorithm>(t, true);
    const auto t2 = std::chrono::steady_clock::now();
    std::cout << "  sword_24: dp_merge cost=" << d.cost << " max_step=" << d.max_step << " classes=" << d.classes << " ("
              << std::chrono::duration<double, std::milli>(t1 - t0).count() << " ms); bb_dp cost=" << b.cost
              << " max_step=" << b.max_step << " (" << std::chrono::duration<double, std::milli>(t2 - t1).count() << " ms)"
              << std::endl;
    expect(d.classes > 0 && b.classes > 0, "sword_24 answered by the fast path");
    // 245 / 259 cross-checked with an independent multiset-DP model of the
    // same cost rules; no 245 plan keeps every step ≤ 39.
    expect_eq(d.cost, 245, "sword_24 unconstrained optimum = 245");
    expect_eq(b.cost, 259, "sword_24 cheapest all-≤39 plan = 259");
    expect(b.max_step <= 39, "bb_dp plan keeps every step ≤ 39");
    expect(d.max_step > 39, "the unconstrained optimum needs a step > 39");
    check_plan(t, d, 25, "sword_24 [dp_merge]");
    check_plan(t, b, 25, "sword_24 [bb_dp]");
    TEST_PASS("weight-class fast path solves 24 books");
}

// ─── Fallbacks ────────────────────────────────────────────────────────────

TEST_CASE("test_weight_class_dp_ineligible") {
    EnchReg reg = make_reg({{1, 5}, {2, 3}, {4, 2}}, {{0, 2}});
    ForgeEngine eng;
    ExecutionContext ctx(0, "wc");
    WeightClassDp::Options opt;
    Item target{ItemType::Equip, 1561, 0, {}};
    target.enchs.insert(E(0, 5));
    target.enchs.insert(E(1, 3));
    const Item sword{ItemType::Equip, 1561, 0, {}};
    auto book = [](std::initializer_list<Ench> es) {
        Item b{ItemType::Book, 0, 0, {}};
        for (const auto& e : es)
            b.enchs.insert(e);
        return b;
    };

    auto ok = WeightClassDp::solve({sword, book({E(0, 5)}), book({E(1, 3)})}, target, reg, eng, opt, ctx);
    expect(ok.status == WeightClassDp::Status::Solved, "plain sword + 2 books is eligible");

    // book target: no equipment base (book-book merges can run either way)
    Item book_target = target;
    book_target.type = ItemType::Book;
    auto r1 = WeightClassDp::solve({book({E(0, 5)}), book({E(1, 3)})}, book_target, reg, eng, opt, ctx);
    expect(r1.status == WeightClassDp::Status::Ineligible, "book target → general search");

    // two books sharing an enchant (level-up merge)
    auto r2 = WeightClassDp::solve({sword, book({E(0, 4)}), book({E(0, 4)})}, target, reg, eng, opt, ctx);
    expect(r2.status == WeightClassDp::Status::Ineligible, "shared enchant → general search");

    // a book enchant conflicting with the base's enchant
    Item smite_sword = sword;
    smite_sword.enchs.insert(E(2, 1));
    auto r3 = WeightClassDp::solve({smite_sword, book({E(0, 5)}), book({E(1, 3)})}, target, reg, eng, opt, ctx);
    expect(r3.status == WeightClassDp::Status::Ineligible, "conflict with base → general search");

    // over the split budget
    WeightClassDp::Options tiny;
    tiny.split_budget = 2;
    auto r4 = WeightClassDp::solve({sword, book({E(0, 5)}), book({E(1, 3)})}, target, reg, eng, tiny, ctx);
    expect(r4.status == WeightClassDp::Status::Ineligible, "over budget → general search");

    // opt-out switch
    SearchConfig sc;
    expect(WeightClassDp::enabled(sc, "dp_merge"), "enabled by default");
    sc.extra["dp_merge.weight_class"] = "off";
    expect(!WeightClassDp::enabled(sc, "dp_merge"), "dp_merge.weight_class=off disables");
    expect(WeightClassDp::enabled(sc, "bb_dp"), "the switch is per strategy");
    TEST_PASS("ineligible shapes and opt-out fall back to the general search");
}
