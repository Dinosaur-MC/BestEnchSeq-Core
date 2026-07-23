#include "framework/test_utils.h"
#include "domain/algorithm/_strategies/astar/AStarMemoryBudget.h"
using namespace algorithm;


void test_budget_from_1gb_9_items() {
    auto b = AStarMemoryBudget::from_memory_mb(1024, 9);
    expect(b.max_explored > 0, "max_explored should be positive");
    expect(b.max_open_set > 0, "max_open_set should be positive");
    expect(b.max_step_pool > 0, "max_step_pool should be positive");
    expect(b.max_items_pool > 0, "max_items_pool should be positive");
    // Step pool is cheapest (16b/entry), should have many entries
    expect(b.max_step_pool >= b.max_open_set,
           "step_pool should have at least as many entries as open_set");
    // Items is most expensive, should have much fewer
    expect(b.max_items_pool <= b.max_step_pool,
           "items_pool should have fewer entries than step_pool");
    std::cout << "PASS: test_budget_from_1gb_9_items" << std::endl;
}

void test_budget_zero_memory() {
    auto b = AStarMemoryBudget::from_memory_mb(0, 9);
    expect(b.max_explored == 0, "zero memory should give zero explored");
    expect(b.max_open_set == 0, "zero memory should give zero open_set");
    std::cout << "PASS: test_budget_zero_memory" << std::endl;
}

void test_budget_reserve_sizes() {
    auto b = AStarMemoryBudget::from_memory_mb(1024, 9);
    expect(b.reserve_step_pool > 0 && b.reserve_step_pool <= b.max_step_pool,
           "step_pool reserve should be <= max");
    expect(b.reserve_open_set > 0 && b.reserve_open_set <= b.max_open_set,
           "open_set reserve should be <= max");
    std::cout << "PASS: test_budget_reserve_sizes" << std::endl;
}

void test_budget_small_memory() {
    auto b = AStarMemoryBudget::from_memory_mb(64, 9);
    expect(b.max_explored > 0, "64 MB should still give positive explored");
    std::cout << "PASS: test_budget_small_memory" << std::endl;
}

int main() {
    std::cout << "=== AStarMemoryBudget Tests ===" << std::endl;
    try {
        test_budget_from_1gb_9_items();
        test_budget_zero_memory();
        test_budget_reserve_sizes();
        test_budget_small_memory();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
