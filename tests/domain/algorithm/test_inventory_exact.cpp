// =============================================================================
// Inventory mode with the exact strategies (dp_merge / bb_dp).
//
//   * corpus: on deterministic random inventories (multi-enchant books, books
//     sharing enchants, enchanted / conflicting equipment, prior work), the two
//     independent exact searches agree, are never worse than hamming (the
//     former inventory default) or than an exact search over DefaultResolver's
//     selection alone, and every plan replays through ForgeEngine using only
//     inventory items;
//   * a fixed case where the per-enchant selection misses a cheaper
//     multi-enchant book (pool 11 vs selection 14);
//   * oversized selections fall back to hamming inside bb_dp;
//   * InventoryPoolResolver keeps the selection, adds only relevant items and
//     respects kPoolMax.
// =============================================================================

#define BESQ_TEST_MAIN
#include "domain/algorithm/_strategies/bb_dp/BBDpAlgorithm.h"
#include "domain/algorithm/_strategies/dp_merge/DPMergeAlgorithm.h"
#include "domain/algorithm/_strategies/hamming/HammingAlgorithm.h"
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/diagnostics/AlgorithmDiagnostics.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/algorithm/resolvers/DefaultResolver.h"
#include "domain/algorithm/resolvers/InventoryPoolResolver.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "framework/test_framework.h"

#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace algorithm;

namespace {

// Java sword enchantments: id → (item multiplier, max level)
enum : uint8_t { SHARP, SMITE, BANE, KNOCK, FIRE, LOOT, SWEEP, UNBR, MEND, VANISH, N_ENCH };
struct Spec {
    int mul, max;
};
constexpr Spec SPEC[N_ENCH] = {{1, 5}, {2, 5}, {2, 5}, {2, 2}, {4, 2}, {4, 3}, {4, 3}, {2, 3}, {4, 1}, {8, 1}};

EnchReg make_reg() {
    std::vector<EnchInfo> infos(N_ENCH);
    std::vector<NSID> gids;
    Equipment eq;
    eq.id = NSID("diamond_sword");
    eq.max_durability = 1561;
    for (uint8_t i = 0; i < N_ENCH; ++i) {
        infos[i].id = i;
        infos[i].mul = static_cast<uint8_t>(SPEC[i].mul);
        infos[i].mul_b = static_cast<uint8_t>(std::max(1, SPEC[i].mul >> 1));
        infos[i].max_lvl = static_cast<uint8_t>(SPEC[i].max);
        infos[i].applicable = true;
        eq.applicable_enchs.insert(i);
        gids.push_back(NSID("e" + std::to_string(i)));
    }
    for (int a : {SHARP, SMITE, BANE}) // the damage enchantments are mutually exclusive
        for (int b : {SHARP, SMITE, BANE})
            if (a != b)
                infos[a].exc_mask |= mask_type{1} << b;
    EnchReg r;
    r.init(std::move(infos), std::move(gids), eq);
    return r;
}

Item book(std::initializer_list<std::pair<int, int>> es, int ppn = 0) {
    Item b{ItemType::Book, 0, static_cast<uint8_t>(ppn), {}};
    for (auto [id, lvl] : es)
        b.enchs.insert(Ench{static_cast<Ench::value_type>(id), static_cast<Ench::value_type>(lvl)});
    return b;
}

Item sword(std::initializer_list<std::pair<int, int>> es = {}, int ppn = 0, int dur = 1561) {
    Item s = book(es, ppn);
    s.type = ItemType::Equip;
    s.dur = static_cast<int16_t>(dur);
    return s;
}

AlgorithmInput inventory_input(const EnchReg& reg, const Item& target, ItemCollection items, int32_t cap) {
    AlgorithmInput in;
    in.config.mode = AlgorithmMode::inventory;
    in.config.forge.platform = MCE::Java;
    in.config.search.max_step_cost = cap;
    in.registry = reg;
    in.target = target;
    InventoryPayload inv;
    inv.available = std::move(items);
    in.data = std::move(inv);
    return in;
}

/// Exact search restricted to DefaultResolver's selection (the pre-pool behaviour).
template <class Base> struct SelectionOnly : Base {
    std::unique_ptr<IResolver> get_resolver() const noexcept override { return std::make_unique<DefaultResolver>(); }
};

struct Outcome {
    bool solved = false;
    int32_t cost = 0;
    int32_t max_step = 0;
    bool pass_b = false;
    std::vector<EnchStep> steps;
};

template <class Algo> Outcome run(const AlgorithmInput& in0) {
    Algo algo;
    AlgorithmInput in = in0;
    ExecutionContext ctx(0, "inv");
    algo.init(in, ctx);
    algo.execute(in, ctx);
    Outcome o;
    auto sols = ctx.get_solutions();
    if (!sols.empty() && !sols[0].steps.empty()) {
        o.solved = true;
        o.cost = sols[0].total_cost;
        o.steps = sols[0].steps;
        for (const auto& s : o.steps)
            o.max_step = std::max(o.max_step, s.cost);
    }
    if (auto d = ctx.consume_exit_diagnostics())
        if (auto* pd = dynamic_cast<PartitionDpDiagnostics*>(d.get()))
            o.pass_b = pd->dp_pass_b_ran;
    return o;
}

/// Every step replays through ForgeEngine; operands are inventory items
/// (each used at most once) or earlier results; the plan ends in one item
/// meeting the target.
void check_plan(const AlgorithmInput& in, const Outcome& o, const std::string& label) {
    ForgeEngine eng(in.config.forge);
    std::vector<Item> stock = in.available();
    std::vector<Item> produced;
    auto take = [](std::vector<Item>& from, const Item& it) {
        for (auto p = from.begin(); p != from.end(); ++p)
            if (*p == it) {
                from.erase(p);
                return true;
            }
        return false;
    };
    int32_t sum = 0;
    for (const auto& s : o.steps) {
        Item r = s.base;
        const int32_t c = eng.forge_into(r, s.sacrifice, in.registry);
        expect(r == s.result && c == s.cost, "step replays through ForgeEngine: " + label);
        expect(take(produced, s.base) || take(stock, s.base), "base is an inventory item or earlier result: " + label);
        expect(take(produced, s.sacrifice) || take(stock, s.sacrifice),
               "sacrifice is an inventory item or earlier result: " + label);
        produced.push_back(s.result);
        sum += c;
    }
    expect_eq(sum, o.cost, "plan total equals sum of steps: " + label);
    expect(produced.size() == 1 && meets_target(produced[0], in.target), "plan ends in one target-meeting item: " + label);
}

} // namespace

// ─── Random corpus ───────────────────────────────────────────────────────

TEST_CASE_TIMEOUT("test_inventory_exact_corpus", 600) {
    std::mt19937 rng(20261122);
    const EnchReg reg = make_reg();
    int instances = 0, solved = 0, hamming_solved = 0, better_than_hamming = 0, better_than_selection = 0;
    for (int trial = 0; trial < 100; ++trial) {
        Item target{ItemType::Equip, 1561, 0, {}};
        std::vector<int> tids = {SHARP, LOOT, UNBR, MEND};
        for (int id : {KNOCK, FIRE, SWEEP})
            if (rng() % 2)
                tids.push_back(id);
        for (int id : tids)
            target.enchs.insert(Ench{static_cast<Ench::value_type>(id), static_cast<Ench::value_type>(SPEC[id].max)});

        ItemCollection items;
        const int n_eq = 1 + static_cast<int>(rng() % 3);
        for (int e = 0; e < n_eq; ++e) {
            Item s = sword({}, static_cast<int>(rng() % 3), 900 + static_cast<int>(rng() % 662));
            for (int id = 0; id < N_ENCH; ++id)
                if (rng() % 4 == 0 && !(id <= BANE && (s.enchs.get_mask() & 7)))
                    s.enchs.insert(
                        Ench{static_cast<Ench::value_type>(id), static_cast<Ench::value_type>(1 + rng() % SPEC[id].max)});
            if (s.enchs.empty())
                s.ppn = 0;
            items.push_back(s);
        }
        for (int id : tids) {
            const int nb = 1 + static_cast<int>(rng() % 2); // keeps pools ≲ 11 items (test time)
            for (int b = 0; b < nb; ++b) {
                const int lvl =
                    (rng() % 3 == 0 && SPEC[id].max > 1) ? 1 + static_cast<int>(rng() % (SPEC[id].max - 1)) : SPEC[id].max;
                Item bk = book({{id, lvl}}, rng() % 4 == 0 ? 1 : 0);
                if (rng() % 3 == 0) {
                    const int o = static_cast<int>(rng() % N_ENCH);
                    if (o != id && !(o <= BANE && id <= BANE))
                        bk.enchs.insert(
                            Ench{static_cast<Ench::value_type>(o), static_cast<Ench::value_type>(1 + rng() % SPEC[o].max)});
                }
                items.push_back(bk);
            }
        }
        for (int j = static_cast<int>(rng() % 3); j > 0; --j) { // junk / conflicting books
            const int o = static_cast<int>(rng() % N_ENCH);
            items.push_back(book({{o, 1 + static_cast<int>(rng() % SPEC[o].max)}}));
        }

        const std::string label = "trial " + std::to_string(trial);
        const AlgorithmInput in = inventory_input(reg, target, items, 0);
        const AlgorithmInput in39 = inventory_input(reg, target, items, 39);
        if (InventoryPoolResolver{}.resolve(in).size() < 2)
            continue;
        ++instances;

        const Outcome dp = run<DPMergeAlgorithm>(in);
        const Outcome bb = run<BBDpAlgorithm>(in); // cap 0: pure min cost, like dp_merge
        const Outcome bb39 = run<BBDpAlgorithm>(in39);
        const Outcome sel = run<SelectionOnly<DPMergeAlgorithm>>(in);
        const Outcome ham = run<HammingAlgorithm>(in);

        expect_eq(dp.solved, bb.solved, "dp_merge / bb_dp agree on reachability: " + label);
        expect_eq(dp.cost, bb.cost, "dp_merge == bb_dp optimum: " + label);
        expect_eq(dp.solved, bb39.solved, "bb_dp (cap 39) reaches whatever dp_merge reaches: " + label);
        if (ham.solved) {
            ++hamming_solved;
            expect(dp.solved && dp.cost <= ham.cost, "exact plan never worse than hamming: " + label);
            better_than_hamming += dp.cost < ham.cost;
        }
        if (sel.solved) {
            expect(dp.solved && dp.cost <= sel.cost, "pool never worse than the selection alone: " + label);
            better_than_selection += dp.cost < sel.cost;
        }
        if (bb39.solved && bb39.max_step > 39)
            expect(bb39.pass_b, "a step > 39 only after Pass B (no ≤39 plan): " + label);
        if (dp.solved) {
            ++solved;
            check_plan(in, dp, label + " [dp_merge]");
            check_plan(in, bb, label + " [bb_dp]");
            check_plan(in39, bb39, label + " [bb_dp cap 39]");
        }
    }
    std::cout << "  " << instances << " inventories, " << solved << " solvable; hamming solved " << hamming_solved
              << ", exact cheaper than hamming on " << better_than_hamming << ", pool cheaper than selection on "
              << better_than_selection << std::endl;
    expect(better_than_hamming > 0 && better_than_selection > 0, "the corpus exercises both improvements");
    TEST_PASS("inventory exact strategies: corpus");
}

// ─── Fixed case: a multi-enchant book the per-enchant selection skips ─────

TEST_CASE("test_inventory_exact_multi_enchant_book") {
    const EnchReg reg = make_reg();
    Item target = sword({{SHARP, 5}, {UNBR, 3}, {MEND, 1}});
    // Each single book is the cheapest per enchant, so DefaultResolver keeps
    // the three singles; the one-merge plan uses the (ppn 1) triple book.
    const ItemCollection items = {sword(), book({{SHARP, 5}}), book({{UNBR, 3}}), book({{MEND, 1}}),
                                  book({{SHARP, 5}, {UNBR, 3}, {MEND, 1}}, 1)};
    const AlgorithmInput in = inventory_input(reg, target, items, 39);

    const Outcome sel = run<SelectionOnly<DPMergeAlgorithm>>(in);
    const Outcome dp = run<DPMergeAlgorithm>(in);
    const Outcome bb = run<BBDpAlgorithm>(in);
    const Outcome ham = run<HammingAlgorithm>(in);
    std::cout << "  selection-only " << sel.cost << ", pool dp_merge " << dp.cost << ", bb_dp " << bb.cost << ", hamming "
              << ham.cost << std::endl;
    expect_eq(sel.cost, 14, "best plan over the three single books = 14");
    expect_eq(dp.cost, 11, "pool finds the one-merge plan: 0 + 1 + (5 + 3 + 2) = 11");
    expect_eq(bb.cost, 11, "bb_dp finds it too");
    expect_eq(static_cast<int>(dp.steps.size()), 1, "one forge: sword ← triple book");
    expect(ham.cost >= sel.cost, "hamming (selection only) cannot do better");
    TEST_PASS("inventory: pool finds the multi-enchant book plan");
}

// ─── Oversized selection: hamming first, exact search when it fails ───────

TEST_CASE_TIMEOUT("test_inventory_exact_large_selection_fallback", 120) {
    const EnchReg reg = make_reg();
    Item target = sword({{SHARP, 5}});
    ItemCollection items = {sword()};
    for (int i = 0; i < 16; ++i) // sixteen sharpness I books combine into V
        items.push_back(book({{SHARP, 1}}));
    const AlgorithmInput in = inventory_input(reg, target, items, 39);

    expect(DefaultResolver{}.resolve(in).size() > BBDpAlgorithm::kInventoryExactMax,
           "the selection itself exceeds the exact-search limit");
    const Outcome ham = run<HammingAlgorithm>(in);
    const Outcome bb = run<BBDpAlgorithm>(in);
    std::cout << "  17-item selection: hamming solved=" << ham.solved << "; bb_dp solved=" << bb.solved << " cost=" << bb.cost
              << " steps=" << bb.steps.size() << std::endl;
    expect(!ham.solved, "hamming's tier pairing cannot build sharpness V from sixteen level-I books");
    expect(bb.solved, "bb_dp falls through to the exact search when hamming finds no plan");
    if (bb.solved)
        check_plan(in, bb, "16 x sharpness I");
    TEST_PASS("inventory: oversized selection — hamming first, exact search as fallback");
}

// ─── InventoryPoolResolver ────────────────────────────────────────────────

TEST_CASE("test_inventory_pool_resolver") {
    const EnchReg reg = make_reg();
    Item target = sword({{SHARP, 5}, {UNBR, 3}});
    ItemCollection items = {sword(),
                            book({{SHARP, 5}}),
                            book({{SHARP, 5}}),
                            book({{UNBR, 3}}),
                            book({{LOOT, 3}}),
                            book({{SHARP, 4}, {UNBR, 2}}),
                            sword({{SMITE, 5}})};
    const AlgorithmInput in = inventory_input(reg, target, items, 39);
    const auto sel = DefaultResolver{}.resolve(in);
    const auto pool = InventoryPoolResolver{}.resolve(in);
    expect(pool.size() >= sel.size() && pool.size() <= InventoryPoolResolver::kPoolMax, "pool size bounds");
    for (size_t i = 0; i < sel.size(); ++i)
        expect(pool[i] == sel[i], "selection kept first, in order (pool[0] is the chosen base)");
    for (size_t i = sel.size(); i < pool.size(); ++i)
        expect(pool[i].type == ItemType::Equip || (pool[i].enchs.get_mask() & target.enchs.get_mask()),
               "extras are equipment or books with a target enchant");
    for (const auto& it : pool)
        expect(!(it.type == ItemType::Book && it.enchs.contains(LOOT) && it.enchs.size() == 1),
               "an irrelevant book is never added");
    expect_eq(pool.size(), items.size() - 1, "every relevant item fits under kPoolMax here");

    // Pool capped at kPoolMax.
    ItemCollection many = {sword()};
    for (int i = 0; i < 30; ++i)
        many.push_back(book({{SHARP, 5}}, i % 3));
    many.push_back(book({{UNBR, 3}}));
    const auto capped = InventoryPoolResolver{}.resolve(inventory_input(reg, target, many, 39));
    expect_eq(capped.size(), InventoryPoolResolver::kPoolMax, "large inventories are capped at kPoolMax");

    // Direct mode is untouched.
    AlgorithmInput direct;
    direct.config.mode = AlgorithmMode::direct;
    direct.registry = reg;
    direct.target = target;
    direct.data = DirectPayload{};
    const auto a = DefaultResolver{}.resolve(direct);
    const auto b = InventoryPoolResolver{}.resolve(direct);
    expect(a == b, "direct mode: identical to DefaultResolver");
    TEST_PASS("InventoryPoolResolver contract");
}
