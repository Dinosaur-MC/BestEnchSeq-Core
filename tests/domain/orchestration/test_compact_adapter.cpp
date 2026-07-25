#include "framework/test_utils.h"
#include "domain/orchestration/orchestration.h"
#include "domain/business/types/Profile.h"
#include "domain/business/types/EquipmentTag.h"
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

// Helper: build a test Profile with sword data
Profile make_sword_profile() {
    Profile profile(NSID("test:compact"));
    profile.add_tag({EquipmentTag::sword(), "sword"});
    profile.add_tag({EquipmentTag::chestplate(), "chestplate"});
    profile.add_equipment({NSID("minecraft:diamond_sword"), "Diamond Sword",
                           EquipmentTag::sword(), 1561});
    for (const auto& ench : make_sword_registry())
        profile.add_enchantment(ench);
    return profile;
}

// Helper: create a SolveRequest for simple direct-mode tests
SolveRequest make_request(const Item& target_item, EnchSet source_enchs = {}) {
    SolveRequest request;
    request.target_item = target_item;
    request.mode = AlgorithmMode::direct;
    request.payload = DirectPayload{std::move(source_enchs)};
    request.forge_config = algorithm::ForgeConfig{};
    request.forge_config.platform = MCE::Java;
    request.search_config = algorithm::SearchConfig{};
    return request;
}

// ─── Test 1: minimal valid input produces correct AlgorithmInput ───
void test_apply_valid_input() {
    auto profile = make_sword_profile();
    Item target_item(NSID("minecraft:diamond_sword"), EnchSet{}, 0, 1561);
    auto request = make_request(target_item);
    auto input = CompactAdapter::apply(profile, request);

    expect(input.target.enchs.empty(), "target should be empty");
    expect(input.items.size() == 1,
           "items should have 1 entry (equipment)");
    expect(input.ench_reg.get_target_equip().max_durability == 1561,
           "equipment durability should be 1561");

    TEST_PASS("test_apply_valid_input");
}

// ─── Test 2: target enchantments are forwarded ───
void test_apply_with_target() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);
    auto request = make_request(target_item);
    auto input = CompactAdapter::apply(profile, request);

    expect(input.target.enchs.size() == 1, "target should have 1 enchantment");
    expect((*input.target.enchs.begin()).level == 5, "target enchantment level should be 5");

    TEST_PASS("test_apply_with_target");
}

// ─── Test 3: unknown enchant ID is silently dropped (no throw) ───
void test_apply_invalid_enchant_id() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("minecraft:nonexistent"), "Nonexistent", 1);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);
    auto request = make_request(target_item);
    auto input = CompactAdapter::apply(profile, request);

    expect(input.target.enchs.empty(),
           "target should be empty (invalid ID silently dropped)");

    TEST_PASS("test_apply_invalid_enchant_id");
}

// ─── Test 4: level exceeding max_level is still forwarded ───
//     (current CompactAdapter::apply does not validate levels)
void test_apply_invalid_level() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 99);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);
    auto request = make_request(target_item);
    auto input = CompactAdapter::apply(profile, request);

    expect(input.target.enchs.size() == 1, "target should have 1 enchantment");
    expect((*input.target.enchs.begin()).level == 99,
           "target enchantment level forwarded as-is (no validation)");

    TEST_PASS("test_apply_invalid_level");
}

// ─── Test 5: inapplicable enchant is excluded from EnchReg ───
void test_apply_inapplicable_enchant() {
    auto profile = make_sword_profile();
    Item target_item(NSID("minecraft:diamond_sword"), EnchSet{}, 0, 1561);
    auto request = make_request(target_item);
    auto input = CompactAdapter::apply(profile, request);

    // Global registry has 3 enchants; only 2 (sharpness, knockback) are
    // sword-applicable. protection is chestplate-only.
    expect(input.ench_reg.size() == 2,
           "ench_reg should only have sword-applicable enchantments (2, not 3)");

    TEST_PASS("test_apply_inapplicable_enchant");
}

// ─── Test 6: prior_penalty is forwarded as-is ───
void test_apply_penalty_forward() {
    auto profile = make_sword_profile();
    Item target_item(NSID("minecraft:diamond_sword"), EnchSet{}, 32, 1561);
    auto request = make_request(target_item);
    auto input = CompactAdapter::apply(profile, request);

    expect(input.items[0].ppn == 32,
           "prior_penalty forwarded as-is (no overflow check)");

    TEST_PASS("test_apply_penalty_forward");
}

// ─── Test 7: ench_reg is pruned to only applicable enchantments ───
void test_pruning_only_applicable() {
    auto profile = make_sword_profile();
    Item target_item(NSID("minecraft:diamond_sword"), EnchSet{}, 0, 1561);
    auto request = make_request(target_item);
    auto input = CompactAdapter::apply(profile, request);

    // Global registry has 3 enchants; only 2 (sharpness, knockback) are sword-applicable
    expect(input.ench_reg.size() == 2,
           "ench_reg should only have sword-applicable enchantments (2, not 3)");

    TEST_PASS("test_pruning_only_applicable");
}

// ─── Test 8: domain → compact preserves data ───
void test_from_domain() {
    auto profile = make_sword_profile();
    Item target_item(NSID("minecraft:diamond_sword"), EnchSet{}, 0, 1561);
    auto request = make_request(target_item);
    auto input = CompactAdapter::apply(profile, request);
    const auto& reg = input.ench_reg;

    // Create a business Item with sharpness 5, prior_penalty 3
    EnchSet business_enchs;
    business_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item domain_item(NSID("minecraft:diamond_sword"), business_enchs, 3, 1561);

    // domain → compact
    auto compact_item = CompactAdapter::from_domain(domain_item, reg);

    expect(compact_item.type == algorithm::ItemType::Equip,
           "compact type should be Equip");
    expect(compact_item.ppn == 3, "compact prior_penalty should be 3");
    expect(compact_item.dur == 1561,
           "compact durability should match max");

    TEST_PASS("test_from_domain");
}

// ─── Test 9: recall returns empty for invalid output ───
void test_recall_empty_output() {
    algorithm::AlgorithmOutput output;
    output.is_valid = false;

    algorithm::AlgorithmInput input;
    input.f_config.platform = MCE::Java;

    auto solutions = CompactAdapter::recall(output, input);
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
