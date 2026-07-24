#include "framework/test_utils.h"
#include "domain/orchestration/components/CompactAdapter.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/business/types/Ench.h"
#include "domain/business/types/EnchSet.h"
#include "domain/business/types/Item.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Item.h"
#include "domain/algorithm/IAlgorithm.h"
#include <stdexcept>

namespace {

// Helper: create a test EnchantmentRegistry with sword-applicable enchants
EnchantmentRegistry make_sword_registry() {
    return EnchantmentRegistry({
        {NSID("sharpness"), "Sharpness", MCE::All, 5, 5,
         1, false, {}, {EquipmentTag::sword()}},
        {NSID("knockback"), "Knockback", MCE::All, 2, 2,
         2, false, {}, {EquipmentTag::sword()}},
        {NSID("protection"), "Protection", MCE::All, 4, 4,
         1, false, {}, {EquipmentTag::chestplate()}},
    });
}

// ─── Test 1: minimal valid input produces correct AlgorithmInput ───
void test_apply_valid_input() {
    Equipment sword{NSID("minecraft:diamond_sword"), "Diamond Sword",
                    EquipmentTag::sword(), 1561};
    auto ench_reg = make_sword_registry();

    algorithm::Item target_item(
        algorithm::ItemType::Equip,
        1561,   // dur
        0,      // ppn
        algorithm::EnchSet{}
    );
    algorithm::EnchSet source_ench;

    auto input = CompactAdapter::apply(target_item, source_ench, sword, ench_reg);

    expect(input.target.enchs.empty(), "target should be empty");
    expect(input.items.size() == 1,
           "items should have 1 entry (equipment)");
    expect(input.ench_reg.get_target_equip().max_durability == 1561,
           "equipment durability should be 1561");

    TEST_PASS("test_apply_valid_input");
}

// ─── Test 2: target enchantments are forwarded ───
void test_apply_with_target() {
    Equipment sword{NSID("minecraft:diamond_sword"), "Diamond Sword",
                    EquipmentTag::sword(), 1561};
    auto ench_reg = make_sword_registry();

    // algorithm::Item with target enchants using global registry indices
    algorithm::Item target_item(
        algorithm::ItemType::Equip,
        1561,
        0,
        algorithm::EnchSet{}
    );
    // sharpness = global index 0
    target_item.enchs.insert(algorithm::Ench(0, 5));
    algorithm::EnchSet source_ench;

    auto input = CompactAdapter::apply(target_item, source_ench, sword, ench_reg);

    expect(input.target.enchs.size() == 1, "target should have 1 enchantment");
    expect((*input.target.enchs.begin()).level == 5, "target enchantment level should be 5");

    TEST_PASS("test_apply_with_target");
}

// ─── Test 3: unknown enchant ID is silently dropped (no throw) ───
void test_apply_invalid_enchant_id() {
    Equipment sword{NSID("minecraft:diamond_sword"), "Diamond Sword",
                    EquipmentTag::sword(), 1561};
    auto ench_reg = make_sword_registry();

    algorithm::Item target_item(
        algorithm::ItemType::Equip,
        1561,
        0,
        algorithm::EnchSet{}
    );
    // -1 is not a valid global registry index — current implementation
    // silently drops unmapped IDs rather than throwing.
    target_item.enchs.insert(algorithm::Ench(-1, 1));
    algorithm::EnchSet source_ench;

    auto input = CompactAdapter::apply(target_item, source_ench, sword, ench_reg);

    expect(input.target.enchs.empty(),
           "target should be empty (invalid ID silently dropped)");

    TEST_PASS("test_apply_invalid_enchant_id");
}

// ─── Test 4: level exceeding max_level is still forwarded ───
//     (current CompactAdapter::apply does not validate levels)
void test_apply_invalid_level() {
    Equipment sword{NSID("minecraft:diamond_sword"), "Diamond Sword",
                    EquipmentTag::sword(), 1561};
    auto ench_reg = make_sword_registry();

    algorithm::Item target_item(
        algorithm::ItemType::Equip,
        1561,
        0,
        algorithm::EnchSet{}
    );
    // sharpness = global index 0, level 99 exceeds max_level (5)
    target_item.enchs.insert(algorithm::Ench(0, 99));
    algorithm::EnchSet source_ench;

    auto input = CompactAdapter::apply(target_item, source_ench, sword, ench_reg);

    expect(input.target.enchs.size() == 1, "target should have 1 enchantment");
    expect((*input.target.enchs.begin()).level == 99,
           "target enchantment level forwarded as-is (no validation)");

    TEST_PASS("test_apply_invalid_level");
}

// ─── Test 5: inapplicable enchant is excluded from EnchReg ───
void test_apply_inapplicable_enchant() {
    Equipment sword{NSID("minecraft:diamond_sword"), "Diamond Sword",
                    EquipmentTag::sword(), 1561};
    auto ench_reg = make_sword_registry();

    algorithm::Item target_item(
        algorithm::ItemType::Equip,
        1561,
        0,
        algorithm::EnchSet{}
    );
    algorithm::EnchSet source_ench;

    auto input = CompactAdapter::apply(target_item, source_ench, sword, ench_reg);

    // Global registry has 3 enchants; only 2 (sharpness, knockback) are
    // sword-applicable. protection is chestplate-only.
    expect(input.ench_reg.size() == 2,
           "ench_reg should only have sword-applicable enchantments (2, not 3)");

    TEST_PASS("test_apply_inapplicable_enchant");
}

// ─── Test 6: prior_penalty is forwarded as-is ───
void test_apply_penalty_forward() {
    Equipment sword{NSID("minecraft:diamond_sword"), "Diamond Sword",
                    EquipmentTag::sword(), 1561};
    auto ench_reg = make_sword_registry();

    algorithm::Item target_item(
        algorithm::ItemType::Equip,
        1561,
        32,   // prior_penalty > 31 — no validation in current apply()
        algorithm::EnchSet{}
    );
    algorithm::EnchSet source_ench;

    auto input = CompactAdapter::apply(target_item, source_ench, sword, ench_reg);

    expect(input.items[0].ppn == 32,
           "prior_penalty forwarded as-is (no overflow check)");

    TEST_PASS("test_apply_penalty_forward");
}

// ─── Test 7: ench_reg is pruned to only applicable enchantments ───
void test_pruning_only_applicable() {
    Equipment sword{NSID("minecraft:diamond_sword"), "Diamond Sword",
                    EquipmentTag::sword(), 1561};
    auto ench_reg = make_sword_registry();

    algorithm::Item target_item(
        algorithm::ItemType::Equip,
        1561,
        0,
        algorithm::EnchSet{}
    );
    algorithm::EnchSet source_ench;

    auto input = CompactAdapter::apply(target_item, source_ench, sword, ench_reg);

    // Global registry has 3 enchants; only 2 (sharpness, knockback) are sword-applicable
    expect(input.ench_reg.size() == 2,
           "ench_reg should only have sword-applicable enchantments (2, not 3)");

    TEST_PASS("test_pruning_only_applicable");
}

// ─── Test 8: domain → compact preserves data ───
// NOTE: The full roundtrip (to_domain) is not tested here because
// to_domain() currently uses numeric strings as placeholder NSIDs
// (e.g. NSID("0")), which fails NSID validation (leading digit).
// This is a known limitation documented in the production code TODOs.
void test_from_domain() {
    Equipment sword{NSID("minecraft:diamond_sword"), "Diamond Sword",
                    EquipmentTag::sword(), 1561};
    auto business_reg = make_sword_registry();

    // Build algorithm::EnchReg via CompactAdapter::apply
    algorithm::Item target_item(
        algorithm::ItemType::Equip,
        1561,
        0,
        algorithm::EnchSet{}
    );
    algorithm::EnchSet source_ench;
    auto input = CompactAdapter::apply(target_item, source_ench, sword, business_reg);
    const auto& reg = input.ench_reg;

    // Create a business Item with sharpness 5, prior_penalty 3
    EnchSet business_enchs;
    business_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item domain_item(sword.id, business_enchs, 3, sword.max_durability);

    // domain → compact
    auto compact_item = CompactAdapter::from_domain(domain_item, reg);

    expect(compact_item.type == algorithm::ItemType::Equip,
           "compact type should be Equip");
    expect(compact_item.ppn == 3, "compact prior_penalty should be 3");
    expect(compact_item.dur == sword.max_durability,
           "compact durability should match max");

    TEST_PASS("test_from_domain");
}

// ─── Test 9: recall returns empty for invalid output ───
void test_recall_empty_output() {
    algorithm::AlgorithmOutput output;
    output.is_valid = false;

    algorithm::AlgorithmInput input;
    input.f_config.platform = MCE::Java;
    EnchSet original_ench;
    Item target_item(NSID("minecraft:diamond_sword"), EnchSet{}, 0, 1561);
    ItemCollection available_items;

    auto solutions = CompactAdapter::recall(
        output, input, original_ench, target_item, available_items);
    expect(solutions.empty(),
           "recall() should return empty vector for is_valid=false");

    TEST_PASS("test_recall_empty_output");
}

} // anonymous namespace

int main() {
    try {
        test_apply_valid_input();
        test_apply_with_target();
        test_apply_invalid_enchant_id();
        test_apply_invalid_level();
        test_apply_inapplicable_enchant();
        test_apply_penalty_forward();
        test_pruning_only_applicable();
        test_from_domain();
        test_recall_empty_output();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
