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
    reg.insert(Equipment{NSID("minecraft:netherite_helmet"), "Netherite Helmet",
                         NSID("#minecraft:helmet"), 407});
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
    reg.insert(EnchInfo{NSID("minecraft:protection"), "Protection",
                        MCE::All, 4, 4, 1, false, {},
                        {NSID("#minecraft:helmet")}});
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
    expect(result.durability == 1561, "default durability = max_durability 1561");
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
    expect(result.durability == 1561, "default durability = max_durability");
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
// Properties block { } — prior_penalty
// ============================================================================

void test_item_parser_prior_penalty() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("diamond_sword{prior_penalty:3}",
                                    ench_reg, eq_reg);
    expect(result.prior_penalty == 3, "prior_penalty should be 3");
    expect(result.durability == 1561, "durability defaults to max_durability");
    expect(result.enchantments.empty(), "no enchants");
    TEST_PASS("test_item_parser_prior_penalty");
}

// ============================================================================
// Properties block { } — durability
// ============================================================================

void test_item_parser_durability_explicit() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("diamond_sword{durability:500}",
                                    ench_reg, eq_reg);
    expect(result.durability == 500, "explicit durability override");
    expect(result.prior_penalty == 0, "prior_penalty default");
    TEST_PASS("test_item_parser_durability_explicit");
}

void test_item_parser_durability_default_max() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    // netherite_helmet has max_durability=407
    auto result = ItemParser::parse("netherite_helmet", ench_reg, eq_reg);
    expect(result.durability == 407, "durability defaults to max_durability 407");
    TEST_PASS("test_item_parser_durability_default_max");
}

void test_item_parser_durability_exceeds_max_throws() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        // diamond_sword has max_durability=1561
        ItemParser::parse("diamond_sword{durability:2000}", ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "durability > max_durability should throw");
    TEST_PASS("test_item_parser_durability_exceeds_max_throws");
}

void test_item_parser_durability_at_max_ok() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("diamond_sword{durability:1561}",
                                    ench_reg, eq_reg);
    expect(result.durability == 1561, "durability at max is allowed");
    TEST_PASS("test_item_parser_durability_at_max_ok");
}

void test_item_parser_durability_zero_allowed() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("diamond_sword{durability:0}",
                                    ench_reg, eq_reg);
    expect(result.durability == 0, "durability 0 is allowed");
    TEST_PASS("test_item_parser_durability_zero_allowed");
}

// ============================================================================
// Properties block { } — combined and edge cases
// ============================================================================

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

void test_item_parser_negative_prior_penalty_throws() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        ItemParser::parse("diamond_sword{prior_penalty:-1}", ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "negative prior_penalty should throw");
    TEST_PASS("test_item_parser_negative_prior_penalty_throws");
}

void test_item_parser_durability_overflow_throws() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        // value beyond int32 range
        ItemParser::parse("diamond_sword{durability:9999999999}", ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "durability beyond int32 range should throw");
    TEST_PASS("test_item_parser_durability_overflow_throws");
}

void test_item_parser_prior_penalty_overflow_throws() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        // 999 does not fit the compact uint8_t ppn; previously truncated to 231.
        ItemParser::parse("diamond_sword{prior_penalty:999}", ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "prior_penalty > 255 should throw");
    TEST_PASS("test_item_parser_prior_penalty_overflow_throws");
}

void test_item_parser_prior_penalty_at_max_ok() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("diamond_sword{prior_penalty:255}",
                                    ench_reg, eq_reg);
    expect(result.prior_penalty == 255, "prior_penalty at max 255 is allowed");
    TEST_PASS("test_item_parser_prior_penalty_at_max_ok");
}

int main() {
    try {
        test_item_parser_bare();
        test_item_parser_with_enchants();
        test_item_parser_no_bracket_close();
        test_item_parser_trailing_content();
        test_item_parser_unknown_equip_throws();
        // prior_penalty
        test_item_parser_prior_penalty();
        // durability defaults and validation
        test_item_parser_durability_explicit();
        test_item_parser_durability_default_max();
        test_item_parser_durability_exceeds_max_throws();
        test_item_parser_durability_at_max_ok();
        test_item_parser_durability_zero_allowed();
        // combined + edge
        test_item_parser_both_properties();
        test_item_parser_properties_only();
        test_item_parser_missing_brace_throws();
        test_item_parser_unknown_property_throws();
        test_item_parser_trailing_after_brace_throws();
        test_item_parser_negative_prior_penalty_throws();
        test_item_parser_durability_overflow_throws();
        test_item_parser_prior_penalty_overflow_throws();
        test_item_parser_prior_penalty_at_max_ok();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
