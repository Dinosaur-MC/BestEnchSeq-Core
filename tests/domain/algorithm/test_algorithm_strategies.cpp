#include "framework/test_utils.h"
#include "framework/test_fixture.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "domain/algorithm/AlgorithmExecutor.h"
#include "domain/algorithm/_strategies/astar/AStarAlgorithm.h"
#include "domain/algorithm/_strategies/dfs/DFSAlgorithm.h"
#include "domain/algorithm/_strategies/hamming/HammingAlgorithm.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include <algorithm>
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
    fx_global.enchants = EnchantmentRegistry({
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
            bool applicable = biz.applicable_equipments.count(EquipmentTag::sword()) > 0;

            algorithm::EnchInfo ai;
            ai.mul = static_cast<uint16_t>(biz.multiplier);
            ai.mul_b = static_cast<uint16_t>(std::max(1, biz.multiplier >> 1));
            ai.max_lvl = static_cast<uint16_t>(biz.max_level);
            ai.applicable = applicable;

            // Build exc_mask: check against previously-added enchantments
            ai.exc_mask.resize(compact_infos.size() / algorithm::MASK_ELEM_SIZE + 1, 0);
            for (size_t j = 0; j < compact_infos.size(); ++j) {
                if (biz.exclusive_set.count(global_ids[j])) {
                    size_t word = j / algorithm::MASK_ELEM_SIZE;
                    size_t bit = j % algorithm::MASK_ELEM_SIZE;
                    ai.exc_mask[word] |= (algorithm::MaskType(1) << bit);
                    if (word < compact_infos[j].exc_mask.size())
                        compact_infos[j].exc_mask[word] |= (algorithm::MaskType(1) << bit);
                }
            }

            compact_infos.push_back(std::move(ai));
        }
        // Populate applicable_enchs on the target equipment before init.
        for (int32_t i = 0; i < static_cast<int32_t>(sorted_enchs.size()); ++i) {
            if (sorted_enchs[i].second.applicable_equipments.count(EquipmentTag::sword()) > 0)
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
