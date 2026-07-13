#include "framework/test_utils.h"
#include "registries/RegistryAccess.h"
#include "registries/CompactedRegistries.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "algorithm/AlgorithmExecutor.h"
#include "algorithm/strategies/GreedyAlgorithm.h"
#include "algorithm/strategies/DFSAlgorithm.h"
#include "algorithm/strategies/AStarAlgorithm.h"
#include "algorithm/strategies/DynamicPenaltyBalancingAlgorithm.h"
#include "algorithm/strategies/HierarchicalMergeAlgorithm.h"
#include "types/ForgeConfig.h"
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
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
