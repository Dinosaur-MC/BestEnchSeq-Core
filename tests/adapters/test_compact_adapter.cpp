#include "framework/test_utils.h"
#include "adapters/CompactAdapter.h"
#include "resolvers/ItemResolver.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/RegistryAccess.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "registries/CompactedRegistries.h"
#include "types/Equipment.h"
#include "types/CompactedTypes.h"
#include "algorithm/IAlgorithm.h"
#include <stdexcept>

namespace {

void setup() {
    registries::categories().initialize();
    registries::enchants().initialize({
        {"sharpness", "Sharpness", MCE::All, 5, 5,
         1, false, {}, {EquipmentCategory::ID_SWORD}},
        {"knockback", "Knockback", MCE::All, 2, 2,
         2, false, {}, {EquipmentCategory::ID_SWORD}},
        {"protection", "Protection", MCE::All, 4, 4,
         1, false, {}, {EquipmentCategory::ID_CHESTPLATE}},
    });
    registries::equipment().initialize({
        {"diamond_sword", "Diamond Sword", EquipmentCategory::ID_SWORD, 1561},
        {"diamond_chestplate", "Diamond Chestplate", EquipmentCategory::ID_CHESTPLATE, 528},
    });
}

Equipment sword{"diamond_sword", "Diamond Sword", EquipmentCategory::ID_SWORD, 1561};
Equipment chestplate{"diamond_chestplate", "Diamond Chestplate",
                      EquipmentCategory::ID_CHESTPLATE, 528};

// ─── Test 1: minimal valid input produces correct AlgorithmInput ───
void test_apply_valid_input() {
    setup();
    CompactAdapter adapter;

    ItemStack target_item(sword, EnchSet{}, 0, sword.max_durability);
    EnchSet original_ench;
    ItemCollection books;
    books.push_back(ItemStack(EnchSet{Ench(0, 5)}, 0));

    auto input = adapter.apply(ResolvedInput{target_item, original_ench, EnchSet{}, books},
                               registries::enchants());

    expect(input.items.size() == 2, "items should have 2 entries (equipment + 1 book)");
    expect(input.target.empty(), "target should be empty");
    expect(input.config.platform == MCE::Java, "platform should be Java");
    expect(input.ench_reg.get_target_equip().name_id == "diamond_sword",
           "equipment name_id should be diamond_sword");

    std::cout << "PASS: test_apply_valid_input" << std::endl;
}

// ─── Test 2: target enchantments are forwarded ───
void test_apply_with_target() {
    setup();
    CompactAdapter adapter;

    ItemStack target_item(sword, EnchSet{}, 0, sword.max_durability);
    EnchSet original_ench;
    EnchSet target{Ench(0, 5)};
    ItemCollection books;

    auto input = adapter.apply(ResolvedInput{target_item, original_ench, target, books},
                               registries::enchants());

    expect(input.target.size() == 1, "target should have 1 enchantment");
    expect(input.target[0].id >= 0, "target enchantment local ID should be >= 0");
    expect(input.target[0].level == 5, "target enchantment level should be 5");

    std::cout << "PASS: test_apply_with_target" << std::endl;
}

// ─── Test 3: invalid enchantment ID is rejected ───
void test_apply_invalid_enchant_id() {
    setup();
    CompactAdapter adapter;

    ItemStack target_item(sword, EnchSet{}, 0, sword.max_durability);
    EnchSet original_ench;
    ItemCollection books;
    // Construct empty book first, then add invalid enchant directly
    // (ItemStack's checked Ench constructor would reject negative ID).
    // then add the invalid enchant directly.
    ItemStack book(EnchSet{}, 0);
    book.enchantments.insert(Ench(-1, 1));
    books.push_back(book);

    bool threw = false;
    try {
        adapter.apply(ResolvedInput{target_item, original_ench, EnchSet{}, books},
                      registries::enchants());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "apply() should throw invalid_argument for invalid enchant ID");

    std::cout << "PASS: test_apply_invalid_enchant_id" << std::endl;
}

// ─── Test 4: level exceeding max_level is rejected ───
void test_apply_invalid_level() {
    setup();
    CompactAdapter adapter;

    ItemStack target_item(sword, EnchSet{}, 0, sword.max_durability);
    EnchSet original_ench;
    ItemCollection books;
    books.push_back(ItemStack(EnchSet{Ench(0, 99)}, 0));

    bool threw = false;
    try {
        adapter.apply(ResolvedInput{target_item, original_ench, EnchSet{}, books},
                      registries::enchants());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "apply() should throw invalid_argument for level exceeding max");

    std::cout << "PASS: test_apply_invalid_level" << std::endl;
}

// ─── Test 5: enchant inapplicable to equipment category is rejected ───
void test_apply_inapplicable_enchant() {
    setup();
    CompactAdapter adapter;

    ItemStack target_item(sword, EnchSet{}, 0, sword.max_durability);
    EnchSet original_ench;
    ItemCollection books;
    // protection (id=2) is only applicable to chestplate, not sword
    books.push_back(ItemStack(EnchSet{Ench(2, 1)}, 0));

    bool threw = false;
    try {
        adapter.apply(ResolvedInput{target_item, original_ench, EnchSet{}, books},
                      registries::enchants());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "apply() should throw invalid_argument for inapplicable enchant");

    std::cout << "PASS: test_apply_inapplicable_enchant" << std::endl;
}

// ─── Test 6: prior_penalty > 31 is rejected ───
void test_apply_penalty_overflow() {
    setup();
    CompactAdapter adapter;

    ItemStack target_item(sword, EnchSet{}, 32, sword.max_durability);
    EnchSet original_ench;
    ItemCollection books;

    bool threw = false;
    try {
        adapter.apply(ResolvedInput{target_item, original_ench, EnchSet{}, books},
                      registries::enchants());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "apply() should throw invalid_argument for prior_penalty > 31");

    std::cout << "PASS: test_apply_penalty_overflow" << std::endl;
}

// ─── Test 7: ench_reg is pruned to only applicable enchantments ───
void test_pruning_only_applicable() {
    setup();
    CompactAdapter adapter;

    ItemStack target_item(sword, EnchSet{}, 0, sword.max_durability);
    EnchSet original_ench;
    ItemCollection books;

    auto input = adapter.apply(ResolvedInput{target_item, original_ench, EnchSet{}, books},
                               registries::enchants());

    // Global registry has 3 enchants; only 2 (sharpness, knockback) are sword-applicable
    expect(input.ench_reg.size() == 2,
           "ench_reg should only have sword-applicable enchantments (2, not 3)");

    const auto& subset_reg = input.ench_reg.get_registry();
    expect(subset_reg.get_id("sharpness") >= 0, "sharpness should be in subset");
    expect(subset_reg.get_id("knockback") >= 0, "knockback should be in subset");
    expect(subset_reg.get_id("protection") < 0, "protection should NOT be in sword subset");

    std::cout << "PASS: test_pruning_only_applicable" << std::endl;
}

// ─── Test 8: domain → compact → domain roundtrip preserves data ───
void test_from_domain_roundtrip() {
    setup();

    compact::EnchReg reg;
    reg.init(registries::enchants(), sword);

    ItemStack domain_item(sword, EnchSet{Ench(0, 5)}, 3, sword.max_durability);

    auto compact_item = CompactAdapter::from_domain(domain_item, reg);

    expect(compact_item.type == compact::ItemType::Equip, "compact type should be Equip");
    expect(compact_item.ppn == 3, "compact prior_penalty should be 3");
    expect(compact_item.dur == sword.max_durability, "compact durability should match max");

    auto back_item = CompactAdapter::to_domain(compact_item, reg);

    expect(back_item.equipment.has_value(), "back_item equipment should not be null");
    expect(back_item.equipment->name_id == "diamond_sword",
           "back_item equipment name_id should match");
    expect(back_item.prior_penalty == 3, "back_item prior_penalty should be 3");

    bool found_sharpness = false;
    for (const auto& e : back_item.enchantments) {
        if (e.id == 0 && e.level == 5) {
            found_sharpness = true;
            break;
        }
    }
    expect(found_sharpness, "back_item should have sharpness 5");

    std::cout << "PASS: test_from_domain_roundtrip" << std::endl;
}

// ─── Test 9: recall returns empty for invalid output ───
void test_recall_empty_output() {
    setup();
    CompactAdapter adapter;

    AlgorithmOutput output;
    output.is_valid = false;

    AlgorithmInput input;
    input.config.platform = MCE::Java;
    ItemStack target_item;
    EnchSet original_ench;
    ItemCollection available_items;

    auto solutions = adapter.recall(output, input, original_ench, target_item, available_items);
    expect(solutions.empty(), "recall() should return empty vector for is_valid=false");

    std::cout << "PASS: test_recall_empty_output" << std::endl;
}

} // anonymous namespace

int main() {
    try {
        test_apply_valid_input();
        test_apply_with_target();
        test_apply_invalid_enchant_id();
        test_apply_invalid_level();
        test_apply_inapplicable_enchant();
        test_apply_penalty_overflow();
        test_pruning_only_applicable();
        test_from_domain_roundtrip();
        test_recall_empty_output();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
