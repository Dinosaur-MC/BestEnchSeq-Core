#include "test_utils.h"
#include "utils/AlgorithmUtils.hpp"
#include "algorithm/forge/DefaultForgeEngine.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/PlatformConfig.h"

namespace {

void setup() {
    std::vector<EnchInfo> infos;
    infos.push_back({"sharpness", "Sharpness", platform::MCE::All, 5, 5,
                      1, {}, {EquipmentCategoryRegistry::ID_SWORD}});
    infos.push_back({"knockback", "Knockback", platform::MCE::All, 2, 2,
                      2, {}, {EquipmentCategoryRegistry::ID_SWORD}});
    infos.push_back({"fire_aspect", "Fire Aspect", platform::MCE::All, 2, 2,
                      2, {}, {EquipmentCategoryRegistry::ID_SWORD}});
    EnchantmentRegistry::get_instance().initialize(infos);
    platform::Config::get_instance().set_active(platform::MCE::Java);
}

Equipment sword{"diamond_sword", "Diamond Sword", EquipmentCategoryRegistry::ID_SWORD, 1561};

void test_admissible_heuristic_all_missing() {
    setup();
    EnchSet current;
    EnchSet target = {Ench(0, 5), Ench(1, 2)};

    int32_t h = AlgorithmUtils::admissible_heuristic(current, target);
    // Sharpness 5: 5 * 1 (book mult) = 5
    // Knockback 2: 2 * 1 (book mult) = 2
    // Total: 7
    expect(h == 7, "heuristic for all missing should be 7, got " + std::to_string(h));
    std::cout << "PASS: test_admissible_heuristic_all_missing" << std::endl;
}

void test_admissible_heuristic_partial() {
    setup();
    EnchSet current = {Ench(0, 3)};  // Sharpness 3
    EnchSet target = {Ench(0, 5), Ench(1, 2)};  // Need Sharpness 5, Knockback 2

    int32_t h = AlgorithmUtils::admissible_heuristic(current, target);
    // Sharpness 5 missing 2 levels: 2 * 1 (book mult) = 2
    // Knockback 2 missing entirely: 2 * 1 (book mult) = 2
    // Total: 4
    expect(h == 4, "heuristic for partial should be 4, got " + std::to_string(h));
    std::cout << "PASS: test_admissible_heuristic_partial" << std::endl;
}

void test_admissible_heuristic_all_satisfied() {
    setup();
    EnchSet current = {Ench(0, 5), Ench(1, 3)};  // Sharpness 5, Knockback 3 (over)
    EnchSet target = {Ench(0, 5), Ench(1, 2)};   // Need Sharpness 5, Knockback 2

    int32_t h = AlgorithmUtils::admissible_heuristic(current, target);
    expect(h == 0, "heuristic for all satisfied should be 0, got " + std::to_string(h));
    std::cout << "PASS: test_admissible_heuristic_all_satisfied" << std::endl;
}

void test_meets_target() {
    setup();

    ItemStack goal(&sword, EnchSet{Ench(0, 5), Ench(1, 2)}, 0, 1561);

    // Exact match
    ItemStack exact(&sword, EnchSet{Ench(0, 5), Ench(1, 2)}, 1, 1561);
    expect(AlgorithmUtils::meets_target(exact, goal), "exact match should pass");

    // Over-leveled
    ItemStack over(&sword, EnchSet{Ench(0, 5), Ench(1, 3)}, 1, 1561);
    expect(AlgorithmUtils::meets_target(over, goal), "over-leveled should pass");

    // Under-leveled
    ItemStack under(&sword, EnchSet{Ench(0, 4), Ench(1, 2)}, 1, 1561);
    expect(!AlgorithmUtils::meets_target(under, goal), "under-leveled should fail");

    // Missing enchantment
    ItemStack missing(&sword, EnchSet{Ench(0, 5)}, 1, 1561);
    expect(!AlgorithmUtils::meets_target(missing, goal), "missing ench should fail");

    // Wrong equipment type
    Equipment other_sword{"other_sword", "Other Sword", EquipmentCategoryRegistry::ID_SWORD, 1561};
    ItemStack wrong_eq(&other_sword, EnchSet{Ench(0, 5), Ench(1, 2)}, 0, 1561);
    expect(!AlgorithmUtils::meets_target(wrong_eq, goal), "wrong equipment should fail");

    std::cout << "PASS: test_meets_target" << std::endl;
}

void test_book_first_merge_one_book() {
    setup();

    DefaultForgeEngine engine;
    ExecutionContext ctx;
    ItemStack equipment(&sword, EnchSet{}, 0, 1561);
    ItemCollection books;
    books.emplace_back(EnchSet{Ench(0, 5)});

    auto result = AlgorithmUtils::book_first_merge(equipment, books, engine, ctx);

    expect(result.total_cost > 0, "cost should be positive");
    expect(result.steps.size() == 1, "one book should produce one step");
    expect(result.equipment.enchantments.find(Ench(0, 5))
           != result.equipment.enchantments.end(), "equipment should have sharpness 5");

    std::cout << "PASS: test_book_first_merge_one_book" << std::endl;
}

void test_book_first_merge_three_books() {
    setup();

    DefaultForgeEngine engine;
    ExecutionContext ctx;
    ItemStack equipment(&sword, EnchSet{}, 0, 1561);
    ItemCollection books;
    books.emplace_back(EnchSet{Ench(0, 5)});   // sharpness 5
    books.emplace_back(EnchSet{Ench(1, 2)});   // knockback 2
    books.emplace_back(EnchSet{Ench(2, 2)});   // fire_aspect 2

    auto result = AlgorithmUtils::book_first_merge(equipment, books, engine, ctx);

    expect(result.total_cost > 0, "cost should be positive");
    expect(result.steps.size() == 3, "three books should produce three steps");
    expect(result.equipment.enchantments.find(Ench(0, 5))
           != result.equipment.enchantments.end(), "equipment should have sharpness 5");
    expect(result.equipment.enchantments.find(Ench(1, 2))
           != result.equipment.enchantments.end(), "equipment should have knockback 2");
    expect(result.equipment.enchantments.find(Ench(2, 2))
           != result.equipment.enchantments.end(), "equipment should have fire_aspect 2");

    std::cout << "PASS: test_book_first_merge_three_books" << std::endl;
}

void test_book_first_merge_empty() {
    setup();

    DefaultForgeEngine engine;
    ExecutionContext ctx;
    ItemStack equipment(&sword, EnchSet{}, 0, 1561);
    ItemCollection books;

    auto result = AlgorithmUtils::book_first_merge(equipment, books, engine, ctx);

    expect(result.total_cost == 0, "empty books should cost 0");
    expect(result.steps.empty(), "empty books should produce no steps");

    std::cout << "PASS: test_book_first_merge_empty" << std::endl;
}

void test_hash_combine() {
    size_t h = 0;
    AlgorithmUtils::hash_combine(h, 42);
    expect(h != 0, "hash combine should produce non-zero result");
    // Same input should produce same output
    size_t h2 = 0;
    AlgorithmUtils::hash_combine(h2, 42);
    expect(h == h2, "hash combine should be deterministic");

    std::cout << "PASS: test_hash_combine" << std::endl;
}

} // namespace

int main() {
    std::cout << "=== AlgorithmUtils Tests ===" << std::endl;
    test_admissible_heuristic_all_missing();
    test_admissible_heuristic_partial();
    test_admissible_heuristic_all_satisfied();
    test_meets_target();
    test_book_first_merge_one_book();
    test_book_first_merge_three_books();
    test_book_first_merge_empty();
    test_hash_combine();
    std::cout << "All AlgorithmUtils tests passed!" << std::endl;
    return 0;
}
