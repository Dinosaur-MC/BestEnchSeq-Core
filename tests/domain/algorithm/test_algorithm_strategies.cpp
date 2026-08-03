#include "framework/test_utils.h"
#include "framework/test_fixture.h"
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "domain/algorithm/AlgorithmExecutor.h"
#include "astar/AStarAlgorithm.h"
#include "domain/algorithm/_strategies/bb_dp/BBDpAlgorithm.h"
#include "dfs/DFSAlgorithm.h"
#include "domain/algorithm/_strategies/dp_merge/DPMergeAlgorithm.h"
#include "domain/algorithm/_strategies/hamming/HammingAlgorithm.h"
#include "domain/algorithm/resolvers/DefaultResolver.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

namespace {

// ─── Namespace aliases for algorithm types ────────────────────────────
using algorithm::DFSAlgorithm;
using algorithm::AStarAlgorithm;
using algorithm::HammingAlgorithm;
using algorithm::BBDpAlgorithm;
using algorithm::DPMergeAlgorithm;
using algorithm::AlgorithmLoader;
using algorithm::EnchCollection;

// ─── Setup helpers (shared across all strategy tests) ─────────────────

constexpr int16_t ID_SHARPNESS = 0;
constexpr int16_t ID_KNOCKBACK = 1;
constexpr int16_t ID_BANE       = 2;  // bane_of_arthropods (conflicts with sharpness)

TestFixture fx_global;

void setup_registries() {
    fx_global.enchants = EnchantmentRegistry({
        {
            NSID("sharpness"), "Sharpness", MCE::All, 5, 5,
            1, false,
            // Vanilla declares sharpness ↔ bane_of_arthropods bidirectionally;
            // the compact conflict-mask builder only sees exclusivity declared
            // by the later-sorted enchant, so both sides must declare it.
            std::unordered_set<NSID>{NSID("bane_of_arthropods")},
            std::unordered_set<NSID>{EquipmentTag::sword()}
        },
        {
            NSID("knockback"), "Knockback", MCE::All, 2, 2,
            2, false,
            std::unordered_set<NSID>{},
            std::unordered_set<NSID>{EquipmentTag::sword()}
        },
        {
            NSID("bane_of_arthropods"), "Bane of Arthropods", MCE::All, 5, 5,
            1, false,
            std::unordered_set<NSID>{NSID("sharpness")},
            std::unordered_set<NSID>{EquipmentTag::sword()}
        },
    });
}

struct TestContext {
    algorithm::EnchReg ench_reg;
    algorithm::ItemCollection items;
    algorithm::Item target_item;

    explicit TestContext(const std::vector<algorithm::Item>& extra_items,
                        const std::vector<algorithm::Ench>& wanted) {
        algorithm::Equipment eq;
        eq.id             = "test";
        eq.max_durability = 1561;

        // Build compact EnchReg from domain enchantment registry
        // Collect enchantments in deterministic order (sorted by NSID string)
        std::vector<std::pair<std::string, EnchInfo>> sorted_enchs;
        sorted_enchs.reserve(fx_global.enchants.size());
        for (const auto& [nsid, info] : fx_global.enchants.data())
            sorted_enchs.emplace_back(nsid.str(), info);
        std::sort(sorted_enchs.begin(), sorted_enchs.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        std::vector<algorithm::EnchInfo> compact_infos;
        std::vector<NSID> global_ids;
        for (int32_t i = 0; i < static_cast<int32_t>(sorted_enchs.size()); ++i)
            global_ids.push_back(NSID(sorted_enchs[i].first));

        // Build compact EnchInfos with pair-wise exclusive_set matching
        for (int32_t i = 0; i < static_cast<int32_t>(sorted_enchs.size()); ++i) {
            const auto& biz = sorted_enchs[i].second;
            bool applicable = biz.supported_items.count(EquipmentTag::sword()) > 0;

            algorithm::EnchInfo ai;
            ai.id = static_cast<uint8_t>(compact_infos.size());
            ai.mul = static_cast<uint8_t>(biz.multiplier);
            ai.mul_b = static_cast<uint8_t>(std::max(1, biz.multiplier >> 1));
            ai.max_lvl = static_cast<uint8_t>(biz.max_level);
            ai.applicable = applicable;

            // Build exc_mask: check against previously-added enchantments
            ai.exc_mask = 0;
            for (size_t j = 0; j < compact_infos.size(); ++j) {
                if (biz.exclusive_set.count(global_ids[j])) {
                    algorithm::mask_type bit = (algorithm::mask_type(1) << j);
                    ai.exc_mask |= bit;
                    compact_infos[j].exc_mask |= bit;
                }
            }

            compact_infos.push_back(std::move(ai));
        }
        // Populate applicable_enchs on the target equipment before init.
        for (int32_t i = 0; i < static_cast<int32_t>(sorted_enchs.size()); ++i) {
            if (sorted_enchs[i].second.supported_items.count(EquipmentTag::sword()) > 0)
                eq.applicable_enchs.insert(static_cast<int16_t>(i));
        }
        ench_reg.init(std::move(compact_infos), std::move(global_ids), eq);

        algorithm::Item equip{algorithm::ItemType::Equip, 1561, 0, {}};
        items.push_back(std::move(equip));
        for (const auto& item : extra_items)
            items.push_back(item);

        // Build target item with wanted enchantments
        target_item = algorithm::Item{algorithm::ItemType::Equip, 1561, 0, {}};
        for (const auto& ench : wanted)
            target_item.enchs.insert(ench);
    }
};

algorithm::Item book(int16_t id, int16_t level) {
    algorithm::Item b{algorithm::ItemType::Book, 0, 0, {}};
    b.enchs.insert(algorithm::Ench{static_cast<algorithm::Ench::value_type>(id), static_cast<algorithm::Ench::value_type>(level)});
    return b;
}

// ─── Run a strategy and return the total cost ─────────────────────────

int32_t run_strategy(std::unique_ptr<algorithm::IAlgorithm> algo,
                     const TestContext& ctx,
                     const algorithm::EnchCollection& source = {}) {
    algorithm::AlgorithmExecutor executor(std::move(algo));

    algorithm::AlgorithmInput input;
    input.config.forge.platform = MCE::Java;
    input.registry = ctx.ench_reg;
    input.target   = ctx.target_item;
    input.config.mode = AlgorithmMode::direct;
    // Direct mode: the resolver builds the base equipment from target + this
    // source and generates the needed books.
    input.data = algorithm::DirectPayload{source};

    executor.start(std::move(input));
    auto state = executor.wait();

    if (state != algorithm::AlgorithmState::Completed)
        return -1;

    auto out = executor.output();
    if (out.solutions.empty())
        return -1;
    if (out.solutions[0].steps.empty())
        return 0;

    return out.solutions[0].total_cost;
}

/// Like run_strategy, but lets a test tweak the AlgorithmConfig first.
int32_t run_strategy_cfg(std::unique_ptr<algorithm::IAlgorithm> algo,
                         const TestContext &ctx,
                         const std::function<void(algorithm::AlgorithmConfig &)> &cfg_fn,
                         const algorithm::EnchCollection &source = {}) {
    algorithm::AlgorithmExecutor executor(std::move(algo));

    algorithm::AlgorithmInput input;
    input.config.forge.platform = MCE::Java;
    input.registry = ctx.ench_reg;
    input.target   = ctx.target_item;
    input.config.mode = AlgorithmMode::direct;
    if (cfg_fn) cfg_fn(input.config);
    input.data = algorithm::DirectPayload{source};

    executor.start(std::move(input));
    auto state = executor.wait();

    if (state != algorithm::AlgorithmState::Completed)
        return -1;

    auto out = executor.output();
    if (out.solutions.empty())
        return -1;
    if (out.solutions[0].steps.empty())
        return 0;

    return out.solutions[0].total_cost;
}

} // anonymous namespace

// ========================================================================
// Test runner (catches exceptions so one failure doesn't crash everything)
// ========================================================================

#define RUN_TEST(name) do { \
    try { name(); } catch (const test_error&) { /* expect() already counted */ } \
    catch (const std::exception& e) { std::cerr << "UNEXPECTED: " << e.what() << std::endl; tests_failed++; } \
    catch (...) { std::cerr << "UNEXPECTED: unknown exception" << std::endl; tests_failed++; } \
} while(0)

// ========================================================================
// DFSAlgorithm tests
// ========================================================================

void test_dfs_simple() {
    auto ctx = TestContext({book(ID_SHARPNESS, 5)}, {{ID_SHARPNESS, 5}});
    auto cost = run_strategy(std::make_unique<DFSAlgorithm>(), ctx);
    expect(cost > 0, "dfs: simple forge should produce positive cost");
    std::cout << "PASS: test_dfs_simple (cost=" << cost << ")" << std::endl;
}

void test_dfs_two_books() {
    auto ctx = TestContext(
        {book(ID_SHARPNESS, 3), book(ID_SHARPNESS, 4)},
        {{ID_SHARPNESS, 4}});
    auto cost = run_strategy(std::make_unique<DFSAlgorithm>(), ctx);
    expect(cost > 0, "dfs: two books should produce positive cost");
    std::cout << "PASS: test_dfs_two_books (cost=" << cost << ")" << std::endl;
}

void test_dfs_target_unreachable() {
    // Conflicting target enchantments (sharpness + bane_of_arthropods are
    // mutually exclusive) are unreachable even though the resolver generates
    // a book for each — the forge can never combine both.
    auto ctx = TestContext({}, {{ID_SHARPNESS, 5}, {ID_BANE, 5}});
    auto cost = run_strategy(std::make_unique<DFSAlgorithm>(), ctx);
    expect(cost == -1, "dfs: unreachable target should return -1");
    std::cout << "PASS: test_dfs_target_unreachable" << std::endl;
}

void test_dfs_target_already_met() {
    // Source already has sharpness V → base meets target → 0-step solution.
    auto ctx = TestContext({}, {{ID_SHARPNESS, 5}});
    algorithm::EnchCollection source;
    source.push_back(algorithm::Ench{ID_SHARPNESS, 5});
    auto cost = run_strategy(std::make_unique<DFSAlgorithm>(), ctx, source);
    expect(cost >= 0, "dfs: target already met should produce result");
    std::cout << "PASS: test_dfs_target_already_met (cost=" << cost << ")" << std::endl;
}

// ========================================================================
// AStarAlgorithm tests
// ========================================================================

void test_astar_simple() {
    auto ctx = TestContext({book(ID_SHARPNESS, 5)}, {{ID_SHARPNESS, 5}});
    auto cost = run_strategy(std::make_unique<AStarAlgorithm>(), ctx);
    expect(cost > 0, "astar: simple forge should produce positive cost");
    std::cout << "PASS: test_astar_simple (cost=" << cost << ")" << std::endl;
}

void test_astar_target_already_met() {
    // Source already has sharpness V → base meets target → GoalAlreadyMet.
    auto ctx = TestContext({}, {{ID_SHARPNESS, 5}});
    algorithm::EnchCollection source;
    source.push_back(algorithm::Ench{ID_SHARPNESS, 5});
    auto cost = run_strategy(std::make_unique<AStarAlgorithm>(), ctx, source);
    expect(cost >= 0, "astar: target already met should produce result");
    std::cout << "PASS: test_astar_target_already_met (cost=" << cost << ")" << std::endl;
}

// ========================================================================
// HammingAlgorithm tests
// ========================================================================

void test_hamming_simple() {
    auto ctx = TestContext({book(ID_SHARPNESS, 5)}, {{ID_SHARPNESS, 5}});
    auto cost = run_strategy(std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost > 0, "hamming: simple forge should produce positive cost");
    std::cout << "PASS: test_hamming_simple (cost=" << cost << ")" << std::endl;
}

void test_hamming_two_books() {
    auto ctx = TestContext(
        {book(ID_SHARPNESS, 3), book(ID_SHARPNESS, 4)},
        {{ID_SHARPNESS, 4}});
    auto cost = run_strategy(std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost > 0, "hamming: two books should produce positive cost");
    std::cout << "PASS: test_hamming_two_books (cost=" << cost << ")" << std::endl;
}

void test_hamming_target_already_met() {
    auto ctx = TestContext({}, {{ID_SHARPNESS, 5}});
    algorithm::EnchCollection source;
    source.push_back(algorithm::Ench{ID_SHARPNESS, 5});
    auto cost = run_strategy(std::make_unique<HammingAlgorithm>(), ctx, source);
    expect(cost >= 0, "hamming: target already met should produce result");
    std::cout << "PASS: test_hamming_target_already_met (cost=" << cost << ")" << std::endl;
}

void test_hamming_target_unreachable() {
    // Conflicting target enchantments (sharpness + bane_of_arthropods) are
    // unreachable — the resolver generates a book for each but they can never
    // combine on the forge.
    auto ctx = TestContext({}, {{ID_SHARPNESS, 5}, {ID_BANE, 5}});
    auto cost = run_strategy(std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost == -1, "hamming: unreachable target should return -1");
    std::cout << "PASS: test_hamming_target_unreachable" << std::endl;
}

void test_hamming_pre_enchanted_equip() {
    // Source already has knockback II → base starts there, needs only the
    // sharpness V book.
    auto ctx = TestContext({}, {{ID_SHARPNESS, 5}, {ID_KNOCKBACK, 2}});
    algorithm::EnchCollection source;
    source.push_back(algorithm::Ench{ID_KNOCKBACK, 2});
    auto cost = run_strategy(std::make_unique<HammingAlgorithm>(), ctx, source);
    expect(cost > 0, "hamming: pre-enchanted equip should produce positive cost");
    std::cout << "PASS: test_hamming_pre_enchanted_equip (cost=" << cost << ")" << std::endl;
}

void test_hamming_durability_repair() {
    auto ctx = TestContext({}, {{ID_SHARPNESS, 5}});
    auto cost = run_strategy(std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost > 0, "hamming: simple forge should produce positive cost");
    std::cout << "PASS: test_hamming_durability_repair (cost=" << cost << ")" << std::endl;
}

// ========================================================================
// BBDpAlgorithm tests
// ========================================================================

void test_bbdp_simple() {
    auto ctx = TestContext({book(ID_SHARPNESS, 5)}, {{ID_SHARPNESS, 5}});
    auto cost = run_strategy(std::make_unique<BBDpAlgorithm>(), ctx);
    expect(cost > 0, "bb_dp: simple forge should produce positive cost");
    std::cout << "PASS: test_bbdp_simple (cost=" << cost << ")" << std::endl;
}

void test_bbdp_two_books() {
    auto ctx = TestContext(
        {book(ID_SHARPNESS, 3), book(ID_SHARPNESS, 4)},
        {{ID_SHARPNESS, 4}});
    auto cost = run_strategy(std::make_unique<BBDpAlgorithm>(), ctx);
    expect(cost > 0, "bb_dp: two books should produce positive cost");
    std::cout << "PASS: test_bbdp_two_books (cost=" << cost << ")" << std::endl;
}

void test_bbdp_target_already_met() {
    auto ctx = TestContext({}, {{ID_SHARPNESS, 5}});
    algorithm::EnchCollection source;
    source.push_back(algorithm::Ench{ID_SHARPNESS, 5});
    auto cost = run_strategy(std::make_unique<BBDpAlgorithm>(), ctx, source);
    expect(cost >= 0, "bb_dp: target already met should produce result");
    std::cout << "PASS: test_bbdp_target_already_met (cost=" << cost << ")" << std::endl;
}

void test_bbdp_target_unreachable() {
    // Conflicting target enchantments (sharpness + bane_of_arthropods) are
    // unreachable — the resolver generates a book for each but they can never
    // combine on the forge.
    auto ctx = TestContext({}, {{ID_SHARPNESS, 5}, {ID_BANE, 5}});
    auto cost = run_strategy(std::make_unique<BBDpAlgorithm>(), ctx);
    expect(cost == -1, "bb_dp: unreachable target should return -1");
    std::cout << "PASS: test_bbdp_target_unreachable" << std::endl;
}

void test_bbdp_pre_enchanted_equip() {
    // Source already has knockback II → base starts there, needs only the
    // sharpness V book.
    auto ctx = TestContext({}, {{ID_SHARPNESS, 5}, {ID_KNOCKBACK, 2}});
    algorithm::EnchCollection source;
    source.push_back(algorithm::Ench{ID_KNOCKBACK, 2});
    auto cost = run_strategy(std::make_unique<BBDpAlgorithm>(), ctx, source);
    expect(cost > 0, "bb_dp: pre-enchanted equip should produce positive cost");
    std::cout << "PASS: test_bbdp_pre_enchanted_equip (cost=" << cost << ")" << std::endl;
}

void test_bbdp_matches_astar() {
    // Two distinct books: both exact solvers must agree on the optimal cost.
    auto ctx = TestContext(
        {book(ID_SHARPNESS, 5), book(ID_KNOCKBACK, 2)},
        {{ID_SHARPNESS, 5}, {ID_KNOCKBACK, 2}});
    auto astar_cost = run_strategy(std::make_unique<AStarAlgorithm>(), ctx);
    auto bbdp_cost  = run_strategy(std::make_unique<BBDpAlgorithm>(), ctx);
    expect(astar_cost > 0 && bbdp_cost == astar_cost,
           "bb_dp: optimal cost should match astar");
    std::cout << "PASS: test_bbdp_matches_astar (cost=" << bbdp_cost
              << ", astar=" << astar_cost << ")" << std::endl;
}

void test_bbdp_max_step_cost() {
    // Default (max_step_cost=39): the solver must still produce a valid
    // result — the feasible optimum if one exists, else the relaxed optimum
    // via the Pass B fallback.
    auto ctx = TestContext(
        {book(ID_SHARPNESS, 5), book(ID_KNOCKBACK, 2)},
        {{ID_SHARPNESS, 5}, {ID_KNOCKBACK, 2}});
    auto cost = run_strategy(std::make_unique<BBDpAlgorithm>(), ctx);
    expect(cost > 0, "bb_dp: default max_step_cost should still solve");
    std::cout << "PASS: test_bbdp_max_step_cost (cost=" << cost << ")" << std::endl;
}

void test_bbdp_max_step_cost_disabled() {
    // max_step_cost=0 → pure total-cost optimization, no feasibility preference.
    auto ctx = TestContext(
        {book(ID_SHARPNESS, 5), book(ID_KNOCKBACK, 2)},
        {{ID_SHARPNESS, 5}, {ID_KNOCKBACK, 2}});
    auto cost = run_strategy_cfg(std::make_unique<BBDpAlgorithm>(), ctx,
        [](algorithm::AlgorithmConfig &c) { c.search.max_step_cost = 0; });
    expect(cost > 0, "bb_dp: max_step_cost=0 should still solve");
    std::cout << "PASS: test_bbdp_max_step_cost_disabled (cost=" << cost << ")" << std::endl;
}

void test_bbdp_beam_width() {
    // Beam-width 4 keeps only the 4 cheapest frontier entries per subset.
    // Result may be sub-optimal but must still meet the target.
    auto ctx = TestContext(
        {book(ID_SHARPNESS, 5), book(ID_KNOCKBACK, 2)},
        {{ID_SHARPNESS, 5}, {ID_KNOCKBACK, 2}});
    auto cost = run_strategy_cfg(std::make_unique<BBDpAlgorithm>(), ctx,
        [](algorithm::AlgorithmConfig &c) { c.search.beam_width = 4; });
    expect(cost > 0, "bb_dp: beam_width=4 should produce a valid solution");
    std::cout << "PASS: test_bbdp_beam_width (cost=" << cost << ")" << std::endl;
}

void test_bbdp_cap_infeasible_fallback() {
    // max_step_cost=1 is stricter than any real step (a sharpness V book costs 5),
    // so no fully ≤cap solution exists.  The solver must fall back to the
    // unconstrained optimum instead of reporting "no solution" (cap is SOFT).
    auto ctx = TestContext(
        {book(ID_SHARPNESS, 5), book(ID_KNOCKBACK, 2)},
        {{ID_SHARPNESS, 5}, {ID_KNOCKBACK, 2}});
    auto cost = run_strategy_cfg(std::make_unique<BBDpAlgorithm>(), ctx,
        [](algorithm::AlgorithmConfig &c) { c.search.max_step_cost = 1; });
    expect(cost > 0, "bb_dp: infeasible cap should fall back to the optimum");
    std::cout << "PASS: test_bbdp_cap_infeasible_fallback (cost=" << cost << ")" << std::endl;
}

void test_bbdp_final_item_meets_target() {
    // A multi-book solution (equipment + 2 distinct books) must replay through
    // IAlgorithm::process() to a final_item that meets the target — guards the
    // balanced-tree replay path used for AlgorithmOutput::final_item.
    auto ctx = TestContext(
        {book(ID_SHARPNESS, 5), book(ID_KNOCKBACK, 2)},
        {{ID_SHARPNESS, 5}, {ID_KNOCKBACK, 2}});
    algorithm::AlgorithmExecutor executor(std::make_unique<BBDpAlgorithm>());
    algorithm::AlgorithmInput input;
    input.config.forge.platform = MCE::Java;
    input.registry = ctx.ench_reg;
    input.target   = ctx.target_item;
    input.config.mode = AlgorithmMode::direct;
    input.data = algorithm::DirectPayload{};
    executor.start(std::move(input));
    auto state = executor.wait();
    auto out = executor.output();
    const bool meets = state == algorithm::AlgorithmState::Completed
        && !out.solutions.empty()
        && meets_target(out.final_item, ctx.target_item);
    expect(meets, "bb_dp: final_item should meet the target");
    std::cout << "PASS: test_bbdp_final_item_meets_target (cost="
              << (out.solutions.empty() ? -1 : out.solutions[0].total_cost) << ")"
              << std::endl;
}

// ─── Book target: an enchanted_book accepts any enchantment; with an empty
// source the resolver emits only the diff books (no empty base book), so a
// two-enchant book merges directly — optimal cost 2 (knockback onto sharpness).
void test_bbdp_book_target() {
    // Reuse the sword registry; the book target is what admits every enchant
    // (the CompactAdapter book exception).  Drive the algorithm directly.
    auto ctx = TestContext({}, {});
    algorithm::AlgorithmExecutor executor(std::make_unique<BBDpAlgorithm>());
    algorithm::AlgorithmInput input;
    input.config.forge.platform = MCE::Java;
    input.registry = ctx.ench_reg;
    input.target   = algorithm::Item{algorithm::ItemType::Book, 0, 0, {}};
    input.target.enchs.insert(algorithm::Ench{ID_SHARPNESS, 5});
    input.target.enchs.insert(algorithm::Ench{ID_KNOCKBACK, 2});
    input.config.mode = AlgorithmMode::direct;
    input.data = algorithm::DirectPayload{};
    algorithm::Item target_copy = input.target;
    executor.start(std::move(input));
    auto state = executor.wait();
    auto out = executor.output();
    expect(state == algorithm::AlgorithmState::Completed,
           "book target solve completed");
    expect(!out.solutions.empty(), "book target produced a solution");
    if (!out.solutions.empty()) {
        expect(out.solutions[0].total_cost == 2,
               "book merge cost should be 2 (knockback onto sharpness)");
        expect(out.final_item.type == algorithm::ItemType::Book,
               "final item is a book");
        expect(meets_target(out.final_item, target_copy),
               "final book meets the book target");
    }
    std::cout << "PASS: test_bbdp_book_target (cost="
              << (out.solutions.empty() ? -1 : out.solutions[0].total_cost) << ")"
              << std::endl;
}

// ========================================================================
// DPMergeAlgorithm tests
// ========================================================================

void test_dpmerge_target_already_met() {
    // Source already has sharpness V → base meets target → 0-step solution.
    auto ctx = TestContext({}, {{ID_SHARPNESS, 5}});
    algorithm::EnchCollection source;
    source.push_back(algorithm::Ench{ID_SHARPNESS, 5});
    auto cost = run_strategy(std::make_unique<DPMergeAlgorithm>(), ctx, source);
    expect(cost >= 0, "dp_merge: target already met should produce result");
    std::cout << "PASS: test_dpmerge_target_already_met (cost=" << cost << ")" << std::endl;
}

void test_dpmerge_source_exceeds_target() {
    // Source sharpness V > target sharpness III → already satisfied → 0-step.
    auto ctx = TestContext({}, {{ID_SHARPNESS, 3}});
    algorithm::EnchCollection source;
    source.push_back(algorithm::Ench{ID_SHARPNESS, 5});
    auto cost = run_strategy(std::make_unique<DPMergeAlgorithm>(), ctx, source);
    expect(cost >= 0, "dp_merge: source exceeding target should produce result");
    std::cout << "PASS: test_dpmerge_source_exceeds_target (cost=" << cost << ")" << std::endl;
}

// ========================================================================
// IAlgorithm::simulate tests — direct already-met must be reachable
// ========================================================================

void test_simulate_direct_already_met() {
    // Direct mode: source == target → simulate must report reachable so the
    // strategy's GoalAlreadyMet path emits a 0-step solution instead of the
    // pipeline short-circuiting with "target unreachable".
    auto ctx = TestContext({}, {{ID_SHARPNESS, 5}});
    algorithm::AlgorithmInput input;
    input.config.mode = AlgorithmMode::direct;
    input.registry    = ctx.ench_reg;
    input.target      = ctx.target_item;
    input.data        = algorithm::DirectPayload{EnchCollection{algorithm::Ench{ID_SHARPNESS, 5}}};

    expect(DFSAlgorithm{}.simulate(input),
           "simulate: source == target direct should be reachable (dfs)");
    expect(AStarAlgorithm{}.simulate(input),
           "simulate: source == target direct should be reachable (astar)");
    expect(HammingAlgorithm{}.simulate(input),
           "simulate: source == target direct should be reachable (hamming)");
    expect(BBDpAlgorithm{}.simulate(input),
           "simulate: source == target direct should be reachable (bb_dp)");
    expect(DPMergeAlgorithm{}.simulate(input),
           "simulate: source == target direct should be reachable (dp_merge)");
    std::cout << "PASS: test_simulate_direct_already_met" << std::endl;
}

void test_simulate_direct_source_exceeds() {
    // Direct mode: source level > target level → already satisfied → reachable.
    auto ctx = TestContext({}, {{ID_SHARPNESS, 3}});
    algorithm::AlgorithmInput input;
    input.config.mode = AlgorithmMode::direct;
    input.registry    = ctx.ench_reg;
    input.target      = ctx.target_item;
    input.data        = algorithm::DirectPayload{EnchCollection{algorithm::Ench{ID_SHARPNESS, 5}}};

    expect(DFSAlgorithm{}.simulate(input),
           "simulate: source level > target level should be reachable");
    std::cout << "PASS: test_simulate_direct_source_exceeds" << std::endl;
}

void test_simulate_direct_below_target() {
    // Direct mode: source below target → books needed → reachable (unchanged).
    auto ctx = TestContext({}, {{ID_SHARPNESS, 5}});
    algorithm::AlgorithmInput input;
    input.config.mode = AlgorithmMode::direct;
    input.registry    = ctx.ench_reg;
    input.target      = ctx.target_item;
    input.data        = algorithm::DirectPayload{EnchCollection{algorithm::Ench{ID_SHARPNESS, 2}}};

    expect(DFSAlgorithm{}.simulate(input),
           "simulate: source below target should be reachable");
    std::cout << "PASS: test_simulate_direct_below_target" << std::endl;
}

void test_simulate_inventory_empty_pool() {
    // Inventory mode: empty pool → unreachable (unchanged).
    auto ctx = TestContext({}, {{ID_SHARPNESS, 5}});
    algorithm::AlgorithmInput input;
    input.config.mode = AlgorithmMode::inventory;
    input.registry    = ctx.ench_reg;
    input.target      = ctx.target_item;
    input.data        = algorithm::InventoryPayload{algorithm::ItemCollection{}, {}};

    expect(!DFSAlgorithm{}.simulate(input),
           "simulate: empty inventory pool should be unreachable");
    std::cout << "PASS: test_simulate_inventory_empty_pool" << std::endl;
}

void test_simulate_inventory_equip_no_book() {
    // Inventory mode: equipment target without a non-empty book → unreachable.
    auto ctx = TestContext({}, {{ID_SHARPNESS, 5}});
    algorithm::AlgorithmInput input;
    input.config.mode = AlgorithmMode::inventory;
    input.registry    = ctx.ench_reg;
    input.target      = ctx.target_item;
    algorithm::Item empty_book{algorithm::ItemType::Book, 0, 0, {}};
    input.data = algorithm::InventoryPayload{algorithm::ItemCollection{empty_book}, {0}};

    expect(!DFSAlgorithm{}.simulate(input),
           "simulate: equipment target without a non-empty book should be unreachable");
    std::cout << "PASS: test_simulate_inventory_equip_no_book" << std::endl;
}

void test_simulate_inventory_with_book() {
    // Inventory mode: a non-empty book present → reachable.
    auto ctx = TestContext({}, {{ID_SHARPNESS, 5}});
    algorithm::AlgorithmInput input;
    input.config.mode = AlgorithmMode::inventory;
    input.registry    = ctx.ench_reg;
    input.target      = ctx.target_item;
    algorithm::Item book_item{algorithm::ItemType::Book, 0, 0, {}};
    book_item.enchs.insert(algorithm::Ench{ID_SHARPNESS, 5});
    input.data = algorithm::InventoryPayload{algorithm::ItemCollection{book_item}, {0}};

    expect(DFSAlgorithm{}.simulate(input),
           "simulate: inventory with a non-empty book should be reachable");
    std::cout << "PASS: test_simulate_inventory_with_book" << std::endl;
}

// ========================================================================
// DefaultResolver inventory selection tests
// ========================================================================

// Compact registry for the resolver selection tests: unbreaking (mul_b=1,
// max 3), mending (mul_b=2, max 1), sharpness (mul_b=1, max 5), thorns
// (mul_b=1, max 3).  Built directly so tests are independent of the sword
// fixture's sharpness/knockback/bane registry.
enum ResolverEnch : uint8_t {
    RE_UNBREAKING = 0,
    RE_MENDING    = 1,
    RE_SHARPNESS  = 2,
    RE_THORNS     = 3,
};

algorithm::EnchReg make_resolver_registry() {
    std::vector<algorithm::EnchInfo> infos(4);
    for (uint8_t i = 0; i < infos.size(); ++i) {
        infos[i].id          = i;
        infos[i].mul         = 1;
        infos[i].mul_b       = 1;
        infos[i].max_lvl     = 3;
        infos[i].applicable  = true;
    }
    infos[RE_UNBREAKING].mul_b = 1; infos[RE_UNBREAKING].max_lvl = 3;
    infos[RE_MENDING].mul_b    = 2; infos[RE_MENDING].max_lvl    = 1;
    infos[RE_SHARPNESS].mul_b  = 1; infos[RE_SHARPNESS].max_lvl  = 5;
    infos[RE_THORNS].mul_b     = 1; infos[RE_THORNS].max_lvl     = 3;

    std::vector<NSID> gids = {NSID("unbreaking"), NSID("mending"),
                              NSID("sharpness"), NSID("thorns")};
    algorithm::Equipment eq;
    eq.id = "test";
    eq.max_durability   = 1561;
    eq.applicable_enchs = {RE_UNBREAKING, RE_MENDING, RE_SHARPNESS, RE_THORNS};

    algorithm::EnchReg reg;
    reg.init(std::move(infos), std::move(gids), eq);
    return reg;
}

algorithm::Item re_book(uint8_t id, uint8_t lvl, uint8_t ppn = 0) {
    algorithm::Item b{algorithm::ItemType::Book, 0, ppn, {}};
    b.enchs.insert(id, lvl);
    return b;
}
algorithm::Item re_equip(uint8_t ppn = 0) {
    return algorithm::Item{algorithm::ItemType::Equip, 1561, ppn, {}};
}
algorithm::Item re_equip_with(uint8_t id, uint8_t lvl, uint8_t ppn = 0) {
    algorithm::Item e{algorithm::ItemType::Equip, 1561, ppn, {}};
    e.enchs.insert(id, lvl);
    return e;
}

/// Run the DefaultResolver inventory pass on a compact input built from \p pool.
algorithm::ResolverOutput run_resolver(algorithm::EnchReg &reg,
                                       const algorithm::Item &target,
                                       std::vector<algorithm::Item> pool) {
    algorithm::AlgorithmInput input;
    input.config.mode = AlgorithmMode::inventory;
    input.registry    = reg;
    input.target      = target;
    input.data        = algorithm::InventoryPayload{std::move(pool), {}};
    return algorithm::DefaultResolver{}.resolve(input);
}

bool has_book_with(const algorithm::ResolverOutput &out, uint8_t id, uint8_t lvl) {
    for (const auto &it : out)
        if (it.type == algorithm::ItemType::Book && it.enchs.contains(id) &&
            it.enchs[id] == lvl)
            return true;
    return false;
}
bool has_book_ppn(const algorithm::ResolverOutput &out, uint8_t ppn) {
    for (const auto &it : out)
        if (it.type == algorithm::ItemType::Book && it.ppn == ppn)
            return true;
    return false;
}
size_t count_books(const algorithm::ResolverOutput &out) {
    size_t n = 0;
    for (const auto &it : out)
        if (it.type == algorithm::ItemType::Book)
            ++n;
    return n;
}
size_t count_equips(const algorithm::ResolverOutput &out) {
    size_t n = 0;
    for (const auto &it : out)
        if (it.type == algorithm::ItemType::Equip)
            ++n;
    return n;
}

void test_resolver_irrelevant_book_dropped() {
    auto reg = make_resolver_registry();
    algorithm::Item target = re_equip();
    target.enchs.insert(RE_UNBREAKING, 3);
    target.enchs.insert(RE_MENDING, 1);
    auto out = run_resolver(reg, target, {
        re_equip(), re_book(RE_UNBREAKING, 2), re_book(RE_UNBREAKING, 3),
        re_book(RE_MENDING, 1), re_book(RE_SHARPNESS, 5)});
    expect(!has_book_with(out, RE_SHARPNESS, 5),
           "resolver: irrelevant sharpness book dropped");
    expect(has_book_with(out, RE_MENDING, 1),
           "resolver: mending book kept");
    expect(has_book_with(out, RE_UNBREAKING, 3),
           "resolver: unbreaking 3 kept (dominates redundant 2)");
    expect(!has_book_with(out, RE_UNBREAKING, 2),
           "resolver: redundant unbreaking 2 dropped");
    expect(count_equips(out) == 1,
           "resolver: single equipment kept");
    TEST_PASS("resolver: irrelevant book dropped (unbreaking+mending target)");
}

void test_resolver_diff_aware_base() {
    // Base already has u2, target u3 → a u2 book suffices (equal-level +1);
    // the u3 book is redundant and dropped (sharpness is irrelevant).
    auto reg = make_resolver_registry();
    algorithm::Item target = re_equip();
    target.enchs.insert(RE_UNBREAKING, 3);
    auto out = run_resolver(reg, target, {
        re_equip_with(RE_UNBREAKING, 2), re_book(RE_UNBREAKING, 2),
        re_book(RE_UNBREAKING, 3), re_book(RE_SHARPNESS, 5)});
    expect(has_book_with(out, RE_UNBREAKING, 2),
           "resolver: u2 book kept (suffices from a u2 base)");
    expect(!has_book_with(out, RE_UNBREAKING, 3),
           "resolver: redundant u3 book dropped");
    expect(!has_book_with(out, RE_SHARPNESS, 5),
           "resolver: irrelevant sharpness dropped");
    expect(out.size() == 2,
           "resolver: base equipment + one book");
    TEST_PASS("resolver: diff-aware — u2 suffices from a u2 base");
}

void test_resolver_prefer_higher_level_book() {
    // Clean base, target u3 → only the u3 book suffices; u2 is dominated.
    auto reg = make_resolver_registry();
    algorithm::Item target = re_equip();
    target.enchs.insert(RE_UNBREAKING, 3);
    auto out = run_resolver(reg, target, {re_equip(), re_book(RE_UNBREAKING, 2),
                                          re_book(RE_UNBREAKING, 3)});
    expect(has_book_with(out, RE_UNBREAKING, 3),
           "resolver: u3 book kept (higher level dominates)");
    expect(!has_book_with(out, RE_UNBREAKING, 2),
           "resolver: u2 book dropped as dominated");
    expect(count_books(out) == 1,
           "resolver: exactly one book kept");
    TEST_PASS("resolver: prefer level 3 over 2 on a clean base");
}

void test_resolver_same_level_lower_ppn() {
    // Two level-3 books at different ppn → keep the lower-ppn one.
    auto reg = make_resolver_registry();
    algorithm::Item target = re_equip();
    target.enchs.insert(RE_UNBREAKING, 3);
    auto out = run_resolver(reg, target, {re_equip(), re_book(RE_UNBREAKING, 3, 2),
                                          re_book(RE_UNBREAKING, 3, 0)});
    expect(count_books(out) == 1,
           "resolver: exactly one u3 book kept");
    expect(has_book_with(out, RE_UNBREAKING, 3) && !has_book_ppn(out, 2),
           "resolver: lower-ppn u3 book kept");
    TEST_PASS("resolver: equal level keeps the lower-ppn book");
}

void test_resolver_best_base_selection() {
    // Two equips: the one already carrying u3 covers the unbreaking gap → its
    // estimated cost is smaller, so it becomes the base; the clean one is
    // redundant and dropped.
    auto reg = make_resolver_registry();
    algorithm::Item target = re_equip();
    target.enchs.insert(RE_UNBREAKING, 3);
    target.enchs.insert(RE_MENDING, 1);
    auto out = run_resolver(reg, target, {
        re_equip(), re_equip_with(RE_UNBREAKING, 3), re_book(RE_MENDING, 1)});
    expect(count_equips(out) == 1,
           "resolver: one equipment kept");
    expect(!out.empty() && out[0].type == algorithm::ItemType::Equip &&
               out[0].enchs[RE_UNBREAKING] == 3,
           "resolver: u3 equipment chosen as best base (first)");
    expect(count_books(out) == 1 && has_book_with(out, RE_MENDING, 1),
           "resolver: only the mending book kept for the remaining gap");
    TEST_PASS("resolver: best base chosen by min estimated cost");
}

void test_resolver_retain_equipment_only_ench() {
    // equipB carries thorns (no book provides it) and a high ppn so it is NOT
    // the best base — but it must be retained because thorns can only come
    // from equipment.
    auto reg = make_resolver_registry();
    algorithm::Item target = re_equip();
    target.enchs.insert(RE_UNBREAKING, 3);
    target.enchs.insert(RE_THORNS, 1);
    auto out = run_resolver(reg, target, {
        re_equip(), re_equip_with(RE_THORNS, 1, 4), re_book(RE_UNBREAKING, 3)});
    expect(count_equips(out) == 2,
           "resolver: thorns-carrying equipment retained");
    bool has_thorns_equip = false;
    for (const auto &it : out)
        if (it.type == algorithm::ItemType::Equip && it.enchs[RE_THORNS] == 1)
            has_thorns_equip = true;
    expect(has_thorns_equip,
           "resolver: retained equipment carries thorns");
    expect(has_book_with(out, RE_UNBREAKING, 3),
           "resolver: u3 book still kept for the unbreaking gap");
    TEST_PASS("resolver: equipment-only enchant retains the second equip");
}

void test_resolver_book_target_pure_book_pool() {
    // Book target + pure-book pool → reachable (non-empty output); irrelevant
    // mending and the dominated u2 are dropped.
    auto reg = make_resolver_registry();
    algorithm::Item target{algorithm::ItemType::Book, 0, 0, {}};
    target.enchs.insert(RE_SHARPNESS, 3);
    auto out = run_resolver(reg, target, {re_book(RE_SHARPNESS, 2),
                                          re_book(RE_SHARPNESS, 3),
                                          re_book(RE_MENDING, 1)});
    expect(!out.empty(),
           "resolver: book target + pure-book pool is reachable");
    expect(has_book_with(out, RE_SHARPNESS, 3),
           "resolver: u3 book kept for the book target");
    expect(!has_book_with(out, RE_SHARPNESS, 2),
           "resolver: redundant u2 book dropped for the book target");
    expect(!has_book_with(out, RE_MENDING, 1),
           "resolver: irrelevant mending dropped for the book target");
    TEST_PASS("resolver: book target + pure-book pool reachable");
}

void test_resolver_book_target_multi_ench() {
    auto reg = make_resolver_registry();
    algorithm::Item target{algorithm::ItemType::Book, 0, 0, {}};
    target.enchs.insert(RE_SHARPNESS, 3);
    target.enchs.insert(RE_MENDING, 1);
    auto out = run_resolver(reg, target, {re_book(RE_SHARPNESS, 3),
                                          re_book(RE_MENDING, 1)});
    expect(count_books(out) == 2 && has_book_with(out, RE_SHARPNESS, 3) &&
               has_book_with(out, RE_MENDING, 1),
           "resolver: both enchant books kept for multi-enchant book target");
    TEST_PASS("resolver: multi-enchant book target keeps needed books");
}

void test_resolver_equip_target_pure_book_pool() {
    // Regression: equipment target + pure-book pool → unreachable (empty).
    auto reg = make_resolver_registry();
    algorithm::Item target = re_equip();
    target.enchs.insert(RE_UNBREAKING, 3);
    auto out = run_resolver(reg, target, {re_book(RE_UNBREAKING, 2),
                                          re_book(RE_UNBREAKING, 3)});
    expect(out.empty(),
           "resolver: equipment target + pure-book pool is unreachable (empty)");
    TEST_PASS("resolver: equip target + pure-book pool unreachable (regression)");
}

void test_resolver_accumulate_books_for_combine() {
    // No single book suffices (u2+u2 merge into u3): both must be kept.
    auto reg = make_resolver_registry();
    algorithm::Item target = re_equip();
    target.enchs.insert(RE_UNBREAKING, 3);
    auto out = run_resolver(reg, target, {re_equip(), re_book(RE_UNBREAKING, 2),
                                          re_book(RE_UNBREAKING, 2)});
    expect(count_books(out) == 2 && has_book_with(out, RE_UNBREAKING, 2),
           "resolver: two u2 books kept to combine into u3");
    TEST_PASS("resolver: accumulate u2+u2 books to reach u3");
}

void test_resolver_output_order() {
    // Equipment first (best base), then books by (level desc, ppn asc): the
    // unbreaking III book (level 3) precedes the mending I book (level 1)
    // despite its higher ppn.
    auto reg = make_resolver_registry();
    algorithm::Item target = re_equip();
    target.enchs.insert(RE_UNBREAKING, 3);
    target.enchs.insert(RE_MENDING, 1);
    auto out = run_resolver(reg, target, {
        re_equip(), re_book(RE_UNBREAKING, 3, 2), re_book(RE_MENDING, 1, 1)});
    expect(out.size() == 3, "resolver: 3 items in output");
    expect(out[0].type == algorithm::ItemType::Equip,
           "resolver: equipment first (best base)");
    expect(out[1].type == algorithm::ItemType::Book &&
               out[1].enchs[RE_UNBREAKING] == 3,
           "resolver: unbreaking III (higher level) book first among books");
    expect(out[2].type == algorithm::ItemType::Book &&
               out[2].enchs[RE_MENDING] == 1,
           "resolver: mending I (lower level) book last");
    TEST_PASS("resolver: output ordering — equip first, books by (level desc, ppn asc)");
}

void test_resolver_retain_equip_when_books_cannot_reach() {
    // Edge case: a thorns-1 book exists, so the old "no book provides it" rule
    // would drop the thorns-3 equipment — but the level-1 book cannot reach the
    // thorns-3 target, so the equipment must be retained (and the pool remains
    // reachable).
    auto reg = make_resolver_registry();
    algorithm::Item target = re_equip();
    target.enchs.insert(RE_UNBREAKING, 3);
    target.enchs.insert(RE_THORNS, 3);
    auto out = run_resolver(reg, target, {
        re_equip(), re_equip_with(RE_THORNS, 3, 4), re_book(RE_UNBREAKING, 3),
        re_book(RE_THORNS, 1)});
    expect(count_equips(out) == 2,
           "resolver: thorns-3 equipment retained (thorns-1 book can't reach 3)");
    bool has_thorns_equip = false;
    for (const auto &it : out)
        if (it.type == algorithm::ItemType::Equip && it.enchs[RE_THORNS] == 3)
            has_thorns_equip = true;
    expect(has_thorns_equip,
           "resolver: retained equipment carries thorns 3");
    expect(!has_book_with(out, RE_THORNS, 1),
           "resolver: thorns-1 book dropped (equipment already covers thorns 3)");
    expect(has_book_with(out, RE_UNBREAKING, 3),
           "resolver: unbreaking book still kept");
    TEST_PASS("resolver: equip retained when books can't reach target level");
}

void test_resolver_preenchanted_equip_reaches_target() {
    // Equipment already meets the target → pool stays non-empty, no books
    // needed, irrelevant books dropped.
    auto reg = make_resolver_registry();
    algorithm::Item target = re_equip();
    target.enchs.insert(RE_UNBREAKING, 3);
    auto out = run_resolver(reg, target, {re_equip_with(RE_UNBREAKING, 3),
                                          re_book(RE_SHARPNESS, 5)});
    expect(!out.empty() && out[0].type == algorithm::ItemType::Equip &&
               out[0].enchs[RE_UNBREAKING] == 3,
           "resolver: pre-enchanted base retained and meets target");
    expect(count_books(out) == 0,
           "resolver: no books needed (gap already met)");
    expect(!has_book_with(out, RE_SHARPNESS, 5),
           "resolver: irrelevant sharpness book dropped");
    TEST_PASS("resolver: pre-enchanted equipment meets target, pool non-empty");
}

// ========================================================================
// AlgorithmLoader validation test
// ========================================================================

void test_loader_registration() {
    AlgorithmLoader loader;
    loader.load_builtin();
    auto names = loader.list();

    expect(!names.empty(), "at least one built-in strategy should be registered");
    expect(!loader.contains("astar"), "astar should NOT be registered (moved to plugin)");
    expect(!loader.contains("dfs"), "dfs should NOT be registered (moved to plugin)");
    expect(loader.contains("hamming"), "hamming should be registered");
    expect(loader.contains("dp_merge"), "dp_merge should be registered");
    expect(loader.contains("bb_dp"), "bb_dp should be registered");
    expect(loader.size() >= 3, "at least 3 built-in strategies");

    for (const auto& name : names) {
        auto algo = loader.create(name);
        expect(algo != nullptr, name + ": algorithm should be creatable");
    }

    std::cout << "PASS: test_loader_registration (strategies: ";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << names[i];
    }
    std::cout << ")" << std::endl;
}

// ========================================================================
// Main
// ========================================================================

int main() {
    std::cout << "=== Algorithm Strategy Tests ===\n\n";

    setup_registries();

    // DFSAlgorithm tests
    RUN_TEST(test_dfs_simple);
    RUN_TEST(test_dfs_two_books);
    RUN_TEST(test_dfs_target_unreachable);
    RUN_TEST(test_dfs_target_already_met);

    // AStarAlgorithm tests
    RUN_TEST(test_astar_simple);
    RUN_TEST(test_astar_target_already_met);

    // HammingAlgorithm tests
    RUN_TEST(test_hamming_simple);
    RUN_TEST(test_hamming_two_books);
    RUN_TEST(test_hamming_target_already_met);
    RUN_TEST(test_hamming_target_unreachable);
    RUN_TEST(test_hamming_pre_enchanted_equip);
    RUN_TEST(test_hamming_durability_repair);

    // BBDpAlgorithm tests
    RUN_TEST(test_bbdp_simple);
    RUN_TEST(test_bbdp_two_books);
    RUN_TEST(test_bbdp_target_already_met);
    RUN_TEST(test_bbdp_target_unreachable);
    RUN_TEST(test_bbdp_pre_enchanted_equip);
    RUN_TEST(test_bbdp_matches_astar);
    RUN_TEST(test_bbdp_max_step_cost);
    RUN_TEST(test_bbdp_max_step_cost_disabled);
    RUN_TEST(test_bbdp_beam_width);
    RUN_TEST(test_bbdp_cap_infeasible_fallback);
    RUN_TEST(test_bbdp_final_item_meets_target);
    RUN_TEST(test_bbdp_book_target);

    // DPMergeAlgorithm tests
    RUN_TEST(test_dpmerge_target_already_met);
    RUN_TEST(test_dpmerge_source_exceeds_target);

    // IAlgorithm::simulate tests
    RUN_TEST(test_simulate_direct_already_met);
    RUN_TEST(test_simulate_direct_source_exceeds);
    RUN_TEST(test_simulate_direct_below_target);
    RUN_TEST(test_simulate_inventory_empty_pool);
    RUN_TEST(test_simulate_inventory_equip_no_book);
    RUN_TEST(test_simulate_inventory_with_book);

    // DefaultResolver inventory selection tests
    RUN_TEST(test_resolver_irrelevant_book_dropped);
    RUN_TEST(test_resolver_diff_aware_base);
    RUN_TEST(test_resolver_prefer_higher_level_book);
    RUN_TEST(test_resolver_same_level_lower_ppn);
    RUN_TEST(test_resolver_best_base_selection);
    RUN_TEST(test_resolver_retain_equipment_only_ench);
    RUN_TEST(test_resolver_book_target_pure_book_pool);
    RUN_TEST(test_resolver_book_target_multi_ench);
    RUN_TEST(test_resolver_equip_target_pure_book_pool);
    RUN_TEST(test_resolver_accumulate_books_for_combine);
    RUN_TEST(test_resolver_output_order);
    RUN_TEST(test_resolver_retain_equip_when_books_cannot_reach);
    RUN_TEST(test_resolver_preenchanted_equip_reaches_target);

    // AlgorithmLoader validation
    RUN_TEST(test_loader_registration);

    std::cout << "\nResults: " << tests_passed << " passed, "
              << tests_failed << " failed, "
              << (tests_passed + tests_failed) << " total" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
