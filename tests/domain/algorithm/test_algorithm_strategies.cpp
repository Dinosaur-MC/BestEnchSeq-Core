#include "framework/test_utils.h"
#include "framework/test_fixture.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentCategoryRegistry.h"
#include "domain/business/types/EquipmentTag.h"
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

// ─── Namespace aliases for algorithm types ────────────────────────────
using algorithm::DFSAlgorithm;
using algorithm::AStarAlgorithm;
using algorithm::HammingAlgorithm;
using algorithm::AlgorithmLoader;
using algorithm::EnchCollection;

// ─── Setup helpers (shared across all strategy tests) ─────────────────

constexpr int16_t ID_SHARPNESS = 0;
constexpr int16_t ID_KNOCKBACK = 1;

TestFixture fx_global;

void setup_registries() {
    fx_global.categories.initialize();
    fx_global.enchants.initialize({
        {
            NSID("sharpness"), "Sharpness", MCE::All, 5, 5,
            1, false,
            std::unordered_set<NSID>{},
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
        eq.id = 0;
        eq.category_id = EquipmentCategory::ID_SWORD;
        eq.max_durability = 1561;

        // Build compact EnchReg from domain enchantment registry
        std::vector<algorithm::EnchInfo> compact_infos;
        std::vector<int32_t> global_ids;
        std::unordered_map<std::string, int32_t> name_to_local;
        for (int32_t i = 0; i < static_cast<int32_t>(fx_global.enchants.size()); ++i) {
            global_ids.push_back(i);
            name_to_local[fx_global.enchants.get(i).id.str()] = i;
        }
        size_t mask_size = (fx_global.enchants.size() + 63) / 64;
        std::vector<std::vector<algorithm::MaskType>> exc_masks(
            fx_global.enchants.size(), std::vector<algorithm::MaskType>(mask_size, 0));
        uint64_t next_group = 0;
        std::vector<bool> visited(fx_global.enchants.size(), false);
        for (int32_t i = 0; i < static_cast<int32_t>(fx_global.enchants.size()); ++i) {
            if (visited[i] || fx_global.enchants.get(i).exclusive_set.empty()) continue;
            uint64_t group_bit = algorithm::MaskType(1) << (next_group % 64);
            next_group++;
            visited[i] = true;
            exc_masks[i][0] |= group_bit;
            for (const auto& ex_nsid : fx_global.enchants.get(i).exclusive_set) {
                auto it = name_to_local.find(ex_nsid.str());
                if (it != name_to_local.end()) {
                    int32_t j = it->second;
                    visited[j] = true;
                    exc_masks[j][0] |= group_bit;
                }
            }
        }
        for (int32_t i = 0; i < static_cast<int32_t>(fx_global.enchants.size()); ++i) {
            const auto& ei = fx_global.enchants.get(i);
            bool applicable = ei.applicable_equipments.count(EquipmentTag::sword()) > 0;
            algorithm::EnchInfo info;
            info.mul = static_cast<uint16_t>(ei.multiplier);
            info.mul_b = static_cast<uint16_t>(ei.multiplier);
            info.max_lvl = static_cast<uint16_t>(ei.max_level);
            info.exc_mask = exc_masks[i];
            info.applicable = applicable;
            compact_infos.push_back(std::move(info));
        }
        ench_reg.init(compact_infos, global_ids, eq);

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
    b.enchs.insert({id, level});
    return b;
}

// ─── Run a strategy and return the total cost ─────────────────────────

int32_t run_strategy(std::unique_ptr<algorithm::IAlgorithm> algo,
                     const TestContext& ctx) {
    algorithm::AlgorithmExecutor executor(std::move(algo));

    algorithm::AlgorithmInput input;
    input.f_config.platform = MCE::Java;
    input.ench_reg = ctx.ench_reg;
    input.items = ctx.items;
    input.target = ctx.target_item;
    // Direct mode: no pre-existing enchants on the target equipment
    input.data = algorithm::EnchCollection{};

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
