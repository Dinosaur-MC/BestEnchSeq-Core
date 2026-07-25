#include "framework/test_utils.h"
#include "domain/interface/cli/ItemParser.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"

// ============================================================================
// Helper: minimal registries
// ============================================================================
static EquipmentRegistry make_eq_reg() {
    EquipmentRegistry reg;
    reg.insert(Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword",
                         NSID("#minecraft:sword"), 1561});
    return reg;
}

static EnchantmentRegistry make_ench_reg() {
    EnchantmentRegistry reg;
    reg.insert(EnchInfo{NSID("minecraft:sharpness"), "Sharpness",
                        MCE::All, 5, 5, 1, false, {},
                        {NSID("#minecraft:sword")}});
    reg.insert(EnchInfo{NSID("minecraft:knockback"), "Knockback",
                        MCE::All, 2, 2, 2, false, {},
                        {NSID("#minecraft:sword")}});
    return reg;
}

// ============================================================================
// Tests
// ============================================================================

void test_item_parser_bare() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("diamond_sword", ench_reg, eq_reg);
    expect(result.id.str() == "minecraft:diamond_sword",
           "id should be diamond_sword");
    expect(result.enchantments.empty(), "no enchantments");
    expect(result.prior_penalty == 0, "default prior_penalty should be 0");
    expect(result.durability == 0, "default durability should be 0");
    TEST_PASS("test_item_parser_bare");
}

void test_item_parser_with_enchants() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("diamond_sword[sharpness=5,knockback=2]",
                                    ench_reg, eq_reg);
    expect(!result.id.str().empty(), "id should be set");
    expect(result.enchantments.size() == 2, "should have 2 enchants");
    expect(result.prior_penalty == 0, "default prior_penalty");
    expect(result.durability == 0, "default durability");
    TEST_PASS("test_item_parser_with_enchants");
}

void test_item_parser_no_bracket_close() {
    EquipmentRegistry empty_eq;
    EnchantmentRegistry empty_ench;
    bool threw = false;
    try {
        ItemParser::parse("sword[sharpness=5", empty_ench, empty_eq);
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "missing closing bracket should throw");
    TEST_PASS("test_item_parser_no_bracket_close");
}

void test_item_parser_trailing_content() {
    EquipmentRegistry empty_eq;
    EnchantmentRegistry empty_ench;
    bool threw = false;
    try {
        ItemParser::parse("sword[a=1][b=2]", empty_ench, empty_eq);
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "second bracket group should throw");
    TEST_PASS("test_item_parser_trailing_content");
}

void test_item_parser_unknown_equip_throws() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        ItemParser::parse("nonexistent_sword", ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "unknown equipment should throw");
    TEST_PASS("test_item_parser_unknown_equip_throws");
}

// ============================================================================
// Properties block { } tests
// ============================================================================

void test_item_parser_prior_penalty() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("diamond_sword{prior_penalty:3}",
                                    ench_reg, eq_reg);
    expect(result.prior_penalty == 3, "prior_penalty should be 3");
    expect(result.durability == 0, "durability default");
    expect(result.enchantments.empty(), "no enchants");
    TEST_PASS("test_item_parser_prior_penalty");
}

void test_item_parser_durability() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("diamond_sword{durability:500}",
                                    ench_reg, eq_reg);
    expect(result.durability == 500, "durability should be 500");
    expect(result.prior_penalty == 0, "prior_penalty default");
    TEST_PASS("test_item_parser_durability");
}

void test_item_parser_both_properties() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse(
        "diamond_sword[sharpness=5]{prior_penalty:2,durability:1000}",
        ench_reg, eq_reg);
    expect(result.enchantments.size() == 1, "one enchant");
    expect(result.prior_penalty == 2, "prior_penalty should be 2");
    expect(result.durability == 1000, "durability should be 1000");
    TEST_PASS("test_item_parser_both_properties");
}

void test_item_parser_properties_only() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse(
        "diamond_sword{prior_penalty:1,durability:250}",
        ench_reg, eq_reg);
    expect(result.enchantments.empty(), "no enchants");
    expect(result.prior_penalty == 1, "prior_penalty should be 1");
    expect(result.durability == 250, "durability should be 250");
    TEST_PASS("test_item_parser_properties_only");
}

void test_item_parser_missing_brace_throws() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        ItemParser::parse("diamond_sword{prior_penalty:3", ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "missing '}' should throw");
    TEST_PASS("test_item_parser_missing_brace_throws");
}

void test_item_parser_unknown_property_throws() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        ItemParser::parse("diamond_sword{invalid_key:1}", ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "unknown property key should throw");
    TEST_PASS("test_item_parser_unknown_property_throws");
}

void test_item_parser_trailing_after_brace_throws() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        ItemParser::parse("diamond_sword{prior_penalty:1}x", ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "trailing content after '}' should throw");
    TEST_PASS("test_item_parser_trailing_after_brace_throws");
}

void test_item_parser_negative_property_throws() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        ItemParser::parse("diamond_sword{prior_penalty:-1}", ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "negative prior_penalty should throw");
    TEST_PASS("test_item_parser_negative_property_throws");
}

int main() {
    try {
        test_item_parser_bare();
        test_item_parser_with_enchants();
        test_item_parser_no_bracket_close();
        test_item_parser_trailing_content();
        test_item_parser_unknown_equip_throws();
        test_item_parser_prior_penalty();
        test_item_parser_durability();
        test_item_parser_both_properties();
        test_item_parser_properties_only();
        test_item_parser_missing_brace_throws();
        test_item_parser_unknown_property_throws();
        test_item_parser_trailing_after_brace_throws();
        test_item_parser_negative_property_throws();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
