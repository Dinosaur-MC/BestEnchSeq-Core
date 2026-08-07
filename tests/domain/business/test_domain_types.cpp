#define BESQ_TEST_MAIN
#include "common/utils/ExpCalculator.hpp"
#include "domain/business/types/Ench.h"
#include "domain/business/types/EnchSet.h"
#include "domain/business/types/Item.h"
#include "domain/business/types/Solution.h"
#include "framework/test_framework.h"

#include <string>

// ─── Ench ───────────────────────────────────────────────────────────────

static const NSID& SHARPNESS() {
    static const NSID id("minecraft:sharpness");
    return id;
}
static const NSID& SMITE() {
    static const NSID id("minecraft:smite");
    return id;
}
static const NSID& UNBREAKING() {
    static const NSID id("minecraft:unbreaking");
    return id;
}
static const NSID& BOOK() {
    static const NSID id("minecraft:book");
    return id;
}

TEST_CASE("test_ench_construct") {
    Ench e(SHARPNESS(), "Sharpness", 3);
    expect(e.id == SHARPNESS(), "ench id sharpness");
    expect(e.level == 3, "ench level 3");
    std::cout << "PASS: test_ench_construct" << std::endl;
}

TEST_CASE("test_ench_default") {
    Ench e;
    expect(e.id.empty(), "default id empty");
    expect(e.level == 1, "default level 1");
    std::cout << "PASS: test_ench_default" << std::endl;
}

TEST_CASE("test_ench_equality") {
    Ench a(SHARPNESS(), "Sharpness", 2);
    Ench b(SHARPNESS(), "Sharpness", 2);
    Ench c(SHARPNESS(), "Sharpness", 3);
    expect(a == b, "same id+level");
    expect(!(a == c), "different level");
    expect(!(b == c), "different level");
    std::cout << "PASS: test_ench_equality" << std::endl;
}

TEST_CASE("test_ench_hash") {
    Ench a(SHARPNESS(), "Sharpness", 4);
    Ench b(SHARPNESS(), "Sharpness", 4);
    Ench c(SMITE(), "Smite", 4);

    std::hash<Ench> hasher;
    expect(hasher(a) == hasher(b), "same values → same hash");
    // Different enchants at same level should have different hash (highly likely)
    expect(hasher(a) != hasher(c), "different enchants → different hash");
    std::cout << "PASS: test_ench_hash" << std::endl;
}

// ─── EnchSet ────────────────────────────────────────────────────────────

TEST_CASE("test_enchset_empty") {
    EnchSet s;
    expect(s.empty(), "default empty");
    expect(s.size() == 0, "size 0");
    std::cout << "PASS: test_enchset_empty" << std::endl;
}

TEST_CASE("test_enchset_insert_and_find") {
    EnchSet s;
    s.emplace(SHARPNESS(), "Sharpness", 2);
    s.emplace(SMITE(), "Smite", 5);
    s.emplace(UNBREAKING(), "Unbreaking", 3);

    expect(s.size() == 3, "3 elements");
    expect(s.find(SHARPNESS()) != s.end(), "find sharpness");
    expect(s.find(SMITE()) != s.end(), "find smite");
    expect(s.find(UNBREAKING()) != s.end(), "find unbreaking");
    expect(s.find(NSID("minecraft:unknown")) == s.end(), "not find unknown");
    std::cout << "PASS: test_enchset_insert_and_find" << std::endl;
}

TEST_CASE("test_enchset_erase") {
    EnchSet s;
    s.emplace(SHARPNESS(), "Sharpness", 1);
    s.emplace(SMITE(), "Smite", 3);
    s.erase(Ench(SHARPNESS(), "Sharpness", 1));
    expect(s.size() == 1, "size 1 after erase");
    expect(s.find(SMITE()) != s.end(), "smite remains");
    expect(s.find(SHARPNESS()) == s.end(), "sharpness gone");
    std::cout << "PASS: test_enchset_erase" << std::endl;
}

// ─── Item ───────────────────────────────────────────────────────────────

TEST_CASE("test_item_default") {
    Item stack;
    expect(stack.enchantments.empty(), "default item has no enchants");
    expect(stack.prior_penalty == 0, "default penalty 0");
    expect(!stack.is_book(), "default item is not a book");
    std::cout << "PASS: test_item_default" << std::endl;
}

TEST_CASE("test_item_book") {
    EnchSet enchants;
    enchants.emplace(SHARPNESS(), "Sharpness", 3);
    enchants.emplace(SMITE(), "Smite", 2);
    Item stack(BOOK(), enchants, 2);
    expect(stack.enchantments.size() == 2, "two enchants");
    expect(stack.prior_penalty == 2, "penalty 2");
    expect(stack.is_book(), "book item");
    std::cout << "PASS: test_item_book" << std::endl;
}

// ─── Item boundaries ─────────────────────────────────────────────────────

TEST_CASE("test_item_boundaries") {
    bool threw = false;
    try {
        Item(NSID("minecraft:diamond_sword"), EnchSet{}, -1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "negative prior_penalty throws invalid_argument");

    threw = false;
    try {
        Item(NSID("minecraft:diamond_sword"), EnchSet{}, 0, -5);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "negative durability throws invalid_argument");

    Item eq(NSID("minecraft:diamond_sword"), EnchSet{}, 0, 1561);
    expect(eq.is_equipment(), "sword item is equipment");
    expect(!eq.is_book(), "sword item is not a book");

    std::cout << "PASS: test_item_boundaries" << std::endl;
}

// ─── Solution derived metrics ───────────────────────────────────────────

TEST_CASE("test_solution_derived_metrics") {
    Solution::EnchStep s1{Item{}, Item{}, 5, 100};
    Solution::EnchStep s2{Item{}, Item{}, 3, 60};

    Solution sol = Solution::make(MCE::Java, EnchSet{}, Item{}, {}, {s1, s2});
    expect_eq(sol.total_exp_level_cost, 8, "make: total level cost = 5+3");
    expect_eq(sol.total_exp_cost, ExpCalculator::level_to_exp(5) + ExpCalculator::level_to_exp(3),
              "make: total exp cost = sum of level_to_exp");
    expect_eq(sol.max_cost_step_index, 0u, "make: peak step is the cost-5 step");
    expect(sol.is_feasible(), "make: feasible with steps");
    expect_eq(sol.get_peak_level_cost(), 5, "peak level cost = 5");

    Solution empty = Solution::make(MCE::Java, EnchSet{}, Item{}, {}, {});
    expect(!empty.is_feasible(), "empty steps → not feasible");
    expect_eq(empty.get_peak_level_cost(), 0, "empty steps → peak 0");

    Solution not_ok = Solution::make(MCE::Java, EnchSet{}, Item{}, {}, {s1}, false);
    expect(!not_ok.is_feasible(), "is_success=false → not feasible");

    Solution bad;
    bad.is_success = true;
    bad.steps = {s1};
    bad.max_cost_step_index = 5; // out of range
    expect_eq(bad.get_peak_level_cost(), 0, "out-of-range peak index → 0");

    std::cout << "PASS: test_solution_derived_metrics" << std::endl;
}

// ─── Main ───────────────────────────────────────────────────────────────
