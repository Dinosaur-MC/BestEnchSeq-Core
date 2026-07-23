#include "framework/test_utils.h"
#include "framework/test_fixture.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentCategoryRegistry.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "domain/algorithm/AlgorithmExecutor.h"
#include "domain/algorithm/_strategies/astar/AStarAlgorithm.h"
#include "domain/algorithm/_strategies/dfs/DFSAlgorithm.h"
#include "domain/algorithm/_strategies/hamming/HammingAlgorithm.h"
#include "domain/orchestration/components/CompactAdapter.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include <memory>
#include <vector>

namespace {

// ─── Setup helpers (shared across all strategy tests) ─────────────────

constexpr int16_t ID_SHARPNESS = 0;
constexpr int16_t ID_KNOCKBACK = 1;

void setup_registries(TestFixture& fx) {
    fx.categories.initialize();
    fx.enchants.initialize({
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
    algorithm::EnchReg ench_reg;
    std::vector<algorithm::Item> items;
    std::vector<algorithm::Ench> target;

    explicit TestContext(const std::vector<algorithm::Item>& extra_items,
                        const std::vector<algorithm::Ench>& wanted) {
        algorithm::Equipment eq;
        eq.id = 0;
        eq.category_id = EquipmentCategory::ID_SWORD;
        eq.max_durability = 1561;
        ench_reg.init(compact_infos, global_ids, eq);

        algorithm::Item equip{algorithm::ItemType::Equip, 1561, 0, {}};
        items.push_back(std::move(equip));
        for (const auto& item : extra_items)
            items.push_back(item);

        target = wanted;
    }
};

algorithm::Item book(int16_t id, int16_t level) {
    algorithm::Item b{algorithm::ItemType::Book, 0, 0, {}};
    b.enchs.insert({id, level});
    return b;
}

// ─── Run a strategy and return the total cost ─────────────────────────

int32_t run_strategy(std::unique_ptr<IAlgorithm> algo,
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
    // Insufficient books should not crash DFS
    auto ctx = TestContext({book(ID_SHARPNESS, 3)}, {{ID_SHARPNESS, 5}});
    auto cost = run_strategy(std::make_unique<DFSAlgorithm>(), ctx);
    expect(cost == -1, "dfs: unreachable target should return -1");
    std::cout << "PASS: test_dfs_target_unreachable" << std::endl;
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
    algorithm::Item equip{algorithm::ItemType::Equip, 1561, 0, {}};
    equip.enchs.insert({ID_SHARPNESS, 5});
    auto ctx = TestContext({book(ID_SHARPNESS, 3)}, {{ID_SHARPNESS, 5}});
    ctx.items[0] = equip;
    auto cost = run_strategy(std::make_unique<AStarAlgorithm>(), ctx);
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
    algorithm::Item equip{algorithm::ItemType::Equip, 1561, 0, {}};
    equip.enchs.insert({ID_SHARPNESS, 5});
    auto ctx = TestContext({book(ID_SHARPNESS, 3)}, {{ID_SHARPNESS, 5}});
    ctx.items[0] = equip;
    auto cost = run_strategy(std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost >= 0, "hamming: target already met should produce result");
    std::cout << "PASS: test_hamming_target_already_met (cost=" << cost << ")" << std::endl;
}

void test_hamming_target_unreachable() {
    auto ctx = TestContext({book(ID_SHARPNESS, 3)}, {{ID_SHARPNESS, 5}});
    auto cost = run_strategy(std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost == -1, "hamming: unreachable target should return -1");
    std::cout << "PASS: test_hamming_target_unreachable" << std::endl;
}

void test_hamming_pre_enchanted_equip() {
    algorithm::Item equip{algorithm::ItemType::Equip, 1561, 0, {}};
    equip.enchs.insert({ID_KNOCKBACK, 2});
    auto ctx = TestContext(
        {book(ID_SHARPNESS, 5)},
        {{ID_SHARPNESS, 5}, {ID_KNOCKBACK, 2}});
    ctx.items[0] = equip;
    auto cost = run_strategy(std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost > 0, "hamming: pre-enchanted equip should produce positive cost");
    std::cout << "PASS: test_hamming_pre_enchanted_equip (cost=" << cost << ")" << std::endl;
}

void test_hamming_durability_repair() {
    algorithm::Item equip{algorithm::ItemType::Equip, 800, 0, {}};
    auto ctx = TestContext({book(ID_SHARPNESS, 5)}, {{ID_SHARPNESS, 5}});
    ctx.items[0] = equip;
    auto cost = run_strategy(std::make_unique<HammingAlgorithm>(), ctx);
    expect(cost > 0, "hamming: damaged equip should produce positive cost");
    std::cout << "PASS: test_hamming_durability_repair (cost=" << cost << ")" << std::endl;
}

// ========================================================================
// AlgorithmLoader validation test
// ========================================================================

void test_loader_registration() {
    AlgorithmLoader loader;
    loader.load_builtin();
    auto names = loader.list();

    expect(!names.empty(), "at least one built-in strategy should be registered");
    expect(loader.contains("astar"), "astar should be registered");
    expect(loader.contains("dfs"), "dfs should be registered");
    expect(loader.contains("hamming"), "hamming should be registered");
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

    // AlgorithmLoader validation
    RUN_TEST(test_loader_registration);

    std::cout << "\nResults: " << tests_passed << " passed, "
              << tests_failed << " failed, "
              << (tests_passed + tests_failed) << " total" << std::endl;
    return tests_failed > 0 ? 1 : 0;
}
