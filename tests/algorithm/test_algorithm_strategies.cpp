#include "framework/test_utils.h"
#include "registries/RegistryAccess.h"
#include "registries/CompactedRegistries.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/strategies/greedy/GreedyAlgorithm.h"
#include "algorithm/strategies/dfs/DFSAlgorithm.h"
#include "algorithm/strategies/astar/AStarAlgorithm.h"
#include "algorithm/strategies/hamming/HammingAlgorithm.h"
#include "algorithm/strategies/diff_first/DiffFirstAlgorithm.h"
#include "algorithm/strategies/hierarchical/HierarchicalMergeAlgorithm.h"
#include "algorithm/strategies/penalty_balance/DynamicPenaltyBalancingAlgorithm.h"
#include "algorithm/strategies/idastar/IDAStarAlgorithm.h"
#include "config/ForgeConfig.h"
#include <memory>
#include <vector>

namespace {

// ─── Setup helpers (shared across all strategy tests) ─────────────────

constexpr int16_t ID_SHARPNESS = 0;
constexpr int16_t ID_KNOCKBACK = 1;

void setup_registries() {
    registries::categories().initialize();
    registries::enchants().initialize({
        {"sharpness", "Sharpness", MCE::All, 5, 5,
         1, false, {}, {EquipmentCategory::ID_SWORD}},
        {"knockback", "Knockback", MCE::All, 2, 2,
         2, false, {}, {EquipmentCategory::ID_SWORD}},
        {"bane_of_arthropods", "Bane of Arthropods", MCE::All, 5, 5,
         1, false, {"sharpness"}, {EquipmentCategory::ID_SWORD}},
    });
}

const Equipment sword{"diamond_sword", "Diamond Sword",
                       EquipmentCategory::ID_SWORD, 1561};

struct TestContext {
    compact::EnchReg ench_reg;
    std::vector<compact::Item> items;
    std::vector<compact::Ench> target;

    explicit TestContext(const std::vector<compact::Item>& extra_items,
                        const std::vector<compact::Ench>& wanted) {
        ench_reg.init(registries::enchants(), sword);

        compact::Item equip{compact::ItemType::Equip, 1561, 0, {}};
        items.push_back(std::move(equip));
        for (const auto& item : extra_items)
            items.push_back(item);

        target = wanted;
    }
};

compact::Item book(int16_t id, int16_t level) {
    compact::Item b{compact::ItemType::Book, 0, 0, {}};
    b.enchs.insert({id, level});
    return b;
}

// ─── Run a strategy and return the total cost ─────────────────────────

int32_t run_strategy(const std::string& name,
                     std::unique_ptr<IAlgorithm> algo,
                     const TestContext& ctx) {
    AlgorithmExecutor executor(std::move(algo));

    AlgorithmInput input;
    input.config.platform = MCE::Java;
    input.ench_reg = ctx.ench_reg;
    input.items = ctx.items;
    input.target = ctx.target;

    executor.start(std::move(input));
    auto state = executor.wait();

    if (state != AlgorithmState::Completed)
        return -1;  // no solution

    auto out = executor.output();
    if (out.solutions.empty())
        return -1;
    // Empty first solution is valid (0 cost, e.g. target already met)
    if (out.solutions[0].steps.empty())
        return 0;

    return out.solutions[0].total_cost;
}

// ─── GreedyAlgorithm tests ───────────────────────────────────────────

void test_greedy_simple() {
    setup_registries();
    TestContext ctx(
        {book(ID_SHARPNESS, 5)},
        {{ID_SHARPNESS, 5}}
    );

    int32_t cost = run_strategy("greedy",
        std::make_unique<GreedyAlgorithm>(), ctx);
    expect(cost > 0, "greedy: simple forge should produce positive cost");
    std::cout << "PASS: test_greedy_simple (cost=" << cost << ")" << std::endl;
}

void test_greedy_target_already_met() {
    setup_registries();
    // Equipment starts with sharpness 5 → target already met
    compact::Item equip{compact::ItemType::Equip, 1561, 0, {}};
    equip.enchs.insert({ID_SHARPNESS, 5});

    TestContext ctx({book(ID_SHARPNESS, 3)}, {{ID_SHARPNESS, 5}});
    ctx.items[0] = equip;  // override equipment with pre-enchanted one

    int32_t cost = run_strategy("greedy",
        std::make_unique<GreedyAlgorithm>(), ctx);
    // Target already met → should produce minimal solution (maybe empty steps)
    expect(cost >= 0, "greedy: target already met should still produce result");
    std::cout << "PASS: test_greedy_target_already_met (cost=" << cost << ")" << std::endl;
}

// ─── DFSAlgorithm tests ──────────────────────────────────────────────

void test_dfs_simple() {
    setup_registries();
    TestContext ctx(
        {book(ID_SHARPNESS, 5)},
        {{ID_SHARPNESS, 5}}
    );

    int32_t cost = run_strategy("dfs",
        std::make_unique<DFSAlgorithm>(), ctx);
    expect(cost > 0, "dfs: simple forge should produce positive cost");
    std::cout << "PASS: test_dfs_simple (cost=" << cost << ")" << std::endl;
}

void test_dfs_two_books() {
    setup_registries();
    // Two books of the same enchant to test level-up combine
    TestContext ctx(
        {book(ID_SHARPNESS, 3), book(ID_SHARPNESS, 4)},
        {{ID_SHARPNESS, 4}}
    );

    int32_t cost = run_strategy("dfs",
        std::make_unique<DFSAlgorithm>(), ctx);
    expect(cost > 0, "dfs: two books should produce positive cost");
    std::cout << "PASS: test_dfs_two_books (cost=" << cost << ")" << std::endl;
}

void test_dfs_target_unreachable() {
    setup_registries();
    // Insufficient books should not crash DFS
    TestContext ctx(
        {book(ID_SHARPNESS, 3)},
        {{ID_SHARPNESS, 5}}
    );

    int32_t cost = run_strategy("dfs",
        std::make_unique<DFSAlgorithm>(), ctx);
    expect(cost == -1, "dfs: unreachable target should return -1");
    std::cout << "PASS: test_dfs_target_unreachable" << std::endl;
}

// ─── AStarAlgorithm tests ────────────────────────────────────────────

void test_astar_simple() {
    setup_registries();
    TestContext ctx(
        {book(ID_SHARPNESS, 5)},
        {{ID_SHARPNESS, 5}}
    );

    int32_t cost = run_strategy("astar",
        std::make_unique<AStarAlgorithm>(), ctx);
    expect(cost > 0, "astar: simple forge should produce positive cost");
    std::cout << "PASS: test_astar_simple (cost=" << cost << ")" << std::endl;
}

void test_astar_target_already_met() {
    setup_registries();
    compact::Item equip{compact::ItemType::Equip, 1561, 0, {}};
    equip.enchs.insert({ID_SHARPNESS, 5});

    TestContext ctx({book(ID_SHARPNESS, 3)}, {{ID_SHARPNESS, 5}});
    ctx.items[0] = equip;

    int32_t cost = run_strategy("astar",
        std::make_unique<AStarAlgorithm>(), ctx);
    expect(cost >= 0, "astar: target already met should produce result");
    std::cout << "PASS: test_astar_target_already_met (cost=" << cost << ")" << std::endl;
}

// ─── DynamicPenaltyBalancingAlgorithm tests ───────────────────────────────────

void test_dpb_simple() {
    setup_registries();
    TestContext ctx(
        {book(ID_SHARPNESS, 5)},
        {{ID_SHARPNESS, 5}}
    );

    int32_t cost = run_strategy("penalty_balance",
        std::make_unique<DynamicPenaltyBalancingAlgorithm>(), ctx);
    expect(cost > 0, "dpb: simple forge should produce positive cost");
    std::cout << "PASS: test_dpb_simple (cost=" << cost << ")" << std::endl;
}

void test_dpb_two_books() {
    setup_registries();
    TestContext ctx(
        {book(ID_SHARPNESS, 4), book(ID_KNOCKBACK, 2)},
        {{ID_SHARPNESS, 4}, {ID_KNOCKBACK, 2}}
    );

    int32_t cost = run_strategy("penalty_balance",
        std::make_unique<DynamicPenaltyBalancingAlgorithm>(), ctx);
    expect(cost > 0, "dpb: two books should produce positive cost");
    std::cout << "PASS: test_dpb_two_books (cost=" << cost << ")" << std::endl;
}

// ─── HierarchicalMergeAlgorithm tests ─────────────────────────────────

void test_hms_simple() {
    setup_registries();
    TestContext ctx(
        {book(ID_SHARPNESS, 5)},
        {{ID_SHARPNESS, 5}}
    );

    int32_t cost = run_strategy("hierarchical",
        std::make_unique<HierarchicalMergeAlgorithm>(), ctx);
    expect(cost > 0, "hms: simple forge should produce positive cost");
    std::cout << "PASS: test_hms_simple (cost=" << cost << ")" << std::endl;
}

void test_hms_many_books() {
    setup_registries();
    // 8 books to trigger the >7 dedup path
    TestContext ctx(
        {book(ID_SHARPNESS, 3), book(ID_SHARPNESS, 3),
         book(ID_SHARPNESS, 3), book(ID_SHARPNESS, 3),
         book(ID_SHARPNESS, 3), book(ID_SHARPNESS, 3),
         book(ID_SHARPNESS, 3), book(ID_SHARPNESS, 3)},
        {{ID_SHARPNESS, 5}}
    );

    int32_t cost = run_strategy("hierarchical",
        std::make_unique<HierarchicalMergeAlgorithm>(), ctx);
    expect(cost > 0, "hms: many books should produce positive cost");
    std::cout << "PASS: test_hms_many_books (cost=" << cost << ")" << std::endl;
}

void test_hms_mixed_tiers() {
    setup_registries();
    // Books with different multipliers to exercise tier grouping
    // sharpness(mult=1) → low tier, knockback(mult=2→bm=1) → low/mid
    TestContext ctx(
        {book(ID_SHARPNESS, 5), book(ID_KNOCKBACK, 2)},
        {{ID_SHARPNESS, 5}, {ID_KNOCKBACK, 2}}
    );

    int32_t cost = run_strategy("hierarchical",
        std::make_unique<HierarchicalMergeAlgorithm>(), ctx);
    expect(cost > 0, "hms: mixed tiers should produce positive cost");
    std::cout << "PASS: test_hms_mixed_tiers (cost=" << cost << ")" << std::endl;
}

// ─── HammingAlgorithm tests ──────────────────────────────────────────

void test_hamming_simple() {
    setup_registries();
    TestContext ctx(
        {book(ID_SHARPNESS, 5)},
        {{ID_SHARPNESS, 5}}
    );

    int32_t cost = run_strategy("hamming",
        std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost > 0, "hamming: simple forge should produce positive cost");
    std::cout << "PASS: test_hamming_simple (cost=" << cost << ")" << std::endl;
}

void test_hamming_two_books() {
    setup_registries();
    // Two different enchants → merged into one book, then into equipment
    TestContext ctx(
        {book(ID_SHARPNESS, 5), book(ID_KNOCKBACK, 2)},
        {{ID_SHARPNESS, 5}, {ID_KNOCKBACK, 2}}
    );

    int32_t cost = run_strategy("hamming",
        std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost > 0, "hamming: two books should produce positive cost");
    std::cout << "PASS: test_hamming_two_books (cost=" << cost << ")" << std::endl;
}

void test_hamming_three_books() {
    setup_registries();
    // Odd count — tests the left-over carry path in the Hamming triangle
    TestContext ctx(
        {book(ID_SHARPNESS, 5), book(ID_SHARPNESS, 3),
         book(ID_KNOCKBACK, 2)},
        {{ID_SHARPNESS, 5}, {ID_KNOCKBACK, 2}}
    );

    int32_t cost = run_strategy("hamming",
        std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost > 0, "hamming: three books (odd count) should produce positive cost");
    std::cout << "PASS: test_hamming_three_books (cost=" << cost << ")" << std::endl;
}

void test_hamming_target_already_met() {
    setup_registries();
    compact::Item equip{compact::ItemType::Equip, 1561, 0, {}};
    equip.enchs.insert({ID_SHARPNESS, 5});

    TestContext ctx({book(ID_SHARPNESS, 3)}, {{ID_SHARPNESS, 5}});
    ctx.items[0] = equip;

    int32_t cost = run_strategy("hamming",
        std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost >= 0, "hamming: target already met should produce result");
    std::cout << "PASS: test_hamming_target_already_met (cost=" << cost << ")" << std::endl;
}

void test_hamming_target_unreachable() {
    setup_registries();
    TestContext ctx(
        {book(ID_SHARPNESS, 3)},
        {{ID_SHARPNESS, 5}}
    );

    int32_t cost = run_strategy("hamming",
        std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost == -1, "hamming: unreachable target should return -1");
    std::cout << "PASS: test_hamming_target_unreachable" << std::endl;
}

void test_hamming_five_books() {
    setup_registries();
    // 5 books — tests popcount arrangement for n > 4 where max-popcount
    // of (n-1) was historically undercounted.
    TestContext ctx(
        {book(ID_SHARPNESS, 5), book(ID_SHARPNESS, 4),
         book(ID_SHARPNESS, 3), book(ID_SHARPNESS, 2),
         book(ID_SHARPNESS, 1)},
        {{ID_SHARPNESS, 5}}
    );

    int32_t cost = run_strategy("hamming",
        std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost > 0, "hamming: five books should produce positive cost");
    std::cout << "PASS: test_hamming_five_books (cost=" << cost << ")" << std::endl;
}

void test_hamming_mixed_penalties() {
    setup_registries();
    // Equipment with prior penalty + books — verifies PPN-seeding works.
    // equip=sharp3(ppn2), books: sharp4(level-up to 4), knock2(new)
    // target sharp4 is reachable (cannot reach sharp5 with only one book)
    compact::Item equip{compact::ItemType::Equip, 1561, 2, {}};  // ppn=2
    equip.enchs.insert({ID_SHARPNESS, 3});

    TestContext ctx(
        {book(ID_SHARPNESS, 4), book(ID_KNOCKBACK, 2)},
        {{ID_SHARPNESS, 4}, {ID_KNOCKBACK, 2}}
    );
    ctx.items[0] = equip;

    int32_t cost = run_strategy("hamming",
        std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost > 0, "hamming: mixed ppn should produce positive cost");
    std::cout << "PASS: test_hamming_mixed_penalties (cost=" << cost << ")" << std::endl;
}

void test_hamming_conflict_enchants() {
    setup_registries();
    // Two mutually exclusive enchantments (sharpness and bane_of_arthropods)
    // → cost should be positive and no crash
    TestContext ctx(
        {book(ID_SHARPNESS, 5), book(2, 3)},  // id 2 = bane_of_arthropods
        {{ID_SHARPNESS, 5}}
    );

    int32_t cost = run_strategy("hamming",
        std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost > 0, "hamming: conflict enchants should produce positive cost");
    std::cout << "PASS: test_hamming_conflict_enchants (cost=" << cost << ")" << std::endl;
}

void test_hamming_level_up() {
    setup_registries();
    // Two books with the same enchantment at the same level should combine.
    // sharpness 3 + sharpness 3 → sharpness 4 (level-up merge)
    TestContext ctx(
        {book(ID_SHARPNESS, 3), book(ID_SHARPNESS, 3)},
        {{ID_SHARPNESS, 4}}
    );

    int32_t cost = run_strategy("hamming",
        std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost > 0, "hamming: same-level books should produce level-up");
    std::cout << "PASS: test_hamming_level_up (cost=" << cost << ")" << std::endl;
}

void test_hamming_seven_books() {
    setup_registries();
    // 7 books + equip = 8 items.  Tests n=8 popcount arrangement where
    // everything divides perfectly into tiers (no leftover at the end).
    TestContext ctx(
        {book(ID_SHARPNESS, 5), book(ID_SHARPNESS, 4),
         book(ID_SHARPNESS, 3), book(ID_SHARPNESS, 2),
         book(ID_SHARPNESS, 1), book(ID_KNOCKBACK, 2),
         book(2, 3)},  // bane_of_arthropods (conflicts with sharpness)
        {{ID_SHARPNESS, 5}, {ID_KNOCKBACK, 2}}
    );

    int32_t cost = run_strategy("hamming",
        std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost > 0, "hamming: seven books should produce positive cost");
    std::cout << "PASS: test_hamming_seven_books (cost=" << cost << ")" << std::endl;
}

void test_hamming_pre_enchanted_equip() {
    setup_registries();
    // Equipment starts with knockback 2, we need to add sharpness 5.
    // Tests the path where equip already has some target enchants.
    compact::Item equip{compact::ItemType::Equip, 1561, 0, {}};
    equip.enchs.insert({ID_KNOCKBACK, 2});

    TestContext ctx(
        {book(ID_SHARPNESS, 5)},
        {{ID_SHARPNESS, 5}, {ID_KNOCKBACK, 2}}
    );
    ctx.items[0] = equip;

    int32_t cost = run_strategy("hamming",
        std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost > 0, "hamming: pre-enchanted equip should produce positive cost");
    std::cout << "PASS: test_hamming_pre_enchanted_equip (cost=" << cost << ")" << std::endl;
}

void test_hamming_durability_repair() {
    setup_registries();
    // Equipment with less-than-max durability to trigger repair cost check.
    // forge_into should still succeed even if the durability mechanic fires.
    compact::Item equip{compact::ItemType::Equip, 800, 0, {}};  // 800 < 1561

    TestContext ctx(
        {book(ID_SHARPNESS, 5)},
        {{ID_SHARPNESS, 5}}
    );
    ctx.items[0] = equip;

    int32_t cost = run_strategy("hamming",
        std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost > 0, "hamming: damaged equip should produce positive cost");
    std::cout << "PASS: test_hamming_durability_repair (cost=" << cost << ")" << std::endl;
}

// ─── DiffFirstAlgorithm tests ───────────────────────────────────────

void test_diff_first_simple() {
    setup_registries();
    TestContext ctx(
        {book(ID_SHARPNESS, 5)},
        {{ID_SHARPNESS, 5}}
    );

    int32_t cost = run_strategy("difficulty_first",
        std::make_unique<DiffFirstAlgorithm>(), ctx);
    expect(cost > 0, "diff_first: simple forge should produce positive cost");
    std::cout << "PASS: test_diff_first_simple (cost=" << cost << ")" << std::endl;
}

void test_diff_first_two_books() {
    setup_registries();
    TestContext ctx(
        {book(ID_SHARPNESS, 5), book(ID_KNOCKBACK, 2)},
        {{ID_SHARPNESS, 5}, {ID_KNOCKBACK, 2}}
    );

    int32_t cost = run_strategy("difficulty_first",
        std::make_unique<DiffFirstAlgorithm>(), ctx);
    expect(cost > 0, "diff_first: two books should produce positive cost");
    std::cout << "PASS: test_diff_first_two_books (cost=" << cost << ")" << std::endl;
}

void test_diff_first_target_unreachable() {
    setup_registries();
    TestContext ctx(
        {book(ID_SHARPNESS, 3)},
        {{ID_SHARPNESS, 5}}
    );

    int32_t cost = run_strategy("difficulty_first",
        std::make_unique<DiffFirstAlgorithm>(), ctx);
    expect(cost == -1, "diff_first: unreachable target should return -1");
    std::cout << "PASS: test_diff_first_target_unreachable" << std::endl;
}

void test_diff_first_mixed_penalties() {
    setup_registries();
    // Equipment with prior penalty — tests PPN seeding path
    compact::Item equip{compact::ItemType::Equip, 1561, 1, {}};  // ppn=1
    equip.enchs.insert({ID_SHARPNESS, 3});

    TestContext ctx(
        {book(ID_SHARPNESS, 4), book(ID_KNOCKBACK, 2)},
        {{ID_SHARPNESS, 4}, {ID_KNOCKBACK, 2}}
    );
    ctx.items[0] = equip;

    int32_t cost = run_strategy("difficulty_first",
        std::make_unique<DiffFirstAlgorithm>(), ctx);
    expect(cost > 0, "diff_first: mixed ppn should produce positive cost");
    std::cout << "PASS: test_diff_first_mixed_penalties (cost=" << cost << ")" << std::endl;
}

void test_diff_first_five_books() {
    setup_registries();
    // 5 books — tests odd-count PPN-tier processing with fallback to mode 1
    TestContext ctx(
        {book(ID_SHARPNESS, 5), book(ID_SHARPNESS, 4),
         book(ID_SHARPNESS, 3), book(ID_SHARPNESS, 2),
         book(ID_SHARPNESS, 1)},
        {{ID_SHARPNESS, 5}}
    );

    int32_t cost = run_strategy("difficulty_first",
        std::make_unique<DiffFirstAlgorithm>(), ctx);
    expect(cost > 0, "diff_first: five books should produce positive cost");
    std::cout << "PASS: test_diff_first_five_books (cost=" << cost << ")" << std::endl;
}

// ─── supported_mode() checks ──────────────────────────────────────────

void test_supported_mode() {
    // Greedy supports both modes
    GreedyAlgorithm greedy;
    expect(bool(greedy.supported_mode() & AlgorithmMode::direct),
           "greedy supports direct");
    expect(bool(greedy.supported_mode() & AlgorithmMode::inventory),
           "greedy supports inventory");

    // Others support direct only
    auto check_direct_only = [](const IAlgorithm& algo, const char* name) {
        expect(bool(algo.supported_mode() & AlgorithmMode::direct),
               std::string(name) + " supports direct");
        expect(!bool(algo.supported_mode() & AlgorithmMode::inventory),
               std::string(name) + " does not support inventory");
    };

    check_direct_only(DFSAlgorithm(), "dfs");
    check_direct_only(AStarAlgorithm(), "astar");
    check_direct_only(IDAStarAlgorithm(), "idastar");
    check_direct_only(DynamicPenaltyBalancingAlgorithm(), "penalty_balance");
    check_direct_only(HierarchicalMergeAlgorithm(), "hierarchical");
    check_direct_only(HammingAlgorithm(), "hamming");
    check_direct_only(DiffFirstAlgorithm(), "diff_first");

    std::cout << "PASS: test_supported_mode" << std::endl;
}

} // anonymous namespace

int main() {
    try {
        // Greedy
        test_greedy_simple();
        test_greedy_target_already_met();

        // DFS
        test_dfs_simple();
        test_dfs_two_books();
        test_dfs_target_unreachable();

        // AStar
        test_astar_simple();
        test_astar_target_already_met();

        // DynamicPenaltyBalancingAlgorithm
        test_dpb_simple();
        test_dpb_two_books();

        // HierarchicalMergeAlgorithm
        test_hms_simple();
        test_hms_many_books();
        test_hms_mixed_tiers();

        // HammingAlgorithm
        test_hamming_simple();
        test_hamming_two_books();
        test_hamming_three_books();
        test_hamming_target_already_met();
        test_hamming_target_unreachable();
        test_hamming_five_books();
        test_hamming_mixed_penalties();
        test_hamming_conflict_enchants();
        test_hamming_level_up();
        test_hamming_seven_books();
        test_hamming_pre_enchanted_equip();
        test_hamming_durability_repair();

        // DiffFirstAlgorithm
        test_diff_first_simple();
        test_diff_first_two_books();
        test_diff_first_target_unreachable();
        test_diff_first_mixed_penalties();
        test_diff_first_five_books();

        // supported_mode
        test_supported_mode();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
