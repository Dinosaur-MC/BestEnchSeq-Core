#define BESQ_TEST_MAIN
#include "framework/test_framework.h"
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

TEST_CASE("test_item_parser_bare") {
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

TEST_CASE("test_item_parser_with_enchants") {
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

TEST_CASE("test_item_parser_no_bracket_close") {
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

TEST_CASE("test_item_parser_trailing_content") {
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

TEST_CASE("test_item_parser_unknown_equip_throws") {
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
// Invalid NSID input maps to the friendly unknown-equipment error (#22)
// ============================================================================

TEST_CASE("test_item_parser_uppercase_maps_to_unknown") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        ItemParser::parse("Diamond_Sword", ench_reg, eq_reg);  // uppercase → invalid NSID
    } catch (const std::exception& e) {
        threw = true;
        std::string msg(e.what());
        expect(msg.find("The NSID") == std::string::npos,
               "raw NSID validator text must not surface");
        expect(msg.find("cli.err.unknown_equipment") != std::string::npos ||
                   msg.find("Unknown equipment") != std::string::npos,
               "uppercase equipment id maps to friendly unknown-equipment error");
    }
    expect(threw, "uppercase equipment id should throw");
    TEST_PASS("test_item_parser_uppercase_maps_to_unknown");
}

TEST_CASE("test_item_parser_dot_segment_maps_to_unknown") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        ItemParser::parse("minecraft:..", ench_reg, eq_reg);  // `..` segment → invalid NSID
    } catch (const std::exception& e) {
        threw = true;
        std::string msg(e.what());
        expect(msg.find("The NSID") == std::string::npos,
               "raw NSID validator text must not surface");
        expect(msg.find("cli.err.unknown_equipment") != std::string::npos ||
                   msg.find("Unknown equipment") != std::string::npos,
               "dot-segment equipment id maps to friendly unknown-equipment error");
    }
    expect(threw, "dot-segment equipment id should throw");
    TEST_PASS("test_item_parser_dot_segment_maps_to_unknown");
}

// ============================================================================
// Properties block { } — prior_penalty
// ============================================================================

TEST_CASE("test_item_parser_prior_penalty") {
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

TEST_CASE("test_item_parser_durability_explicit") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("diamond_sword{durability:500}",
                                    ench_reg, eq_reg);
    expect(result.durability == 500, "explicit durability override");
    expect(result.prior_penalty == 0, "prior_penalty default");
    TEST_PASS("test_item_parser_durability_explicit");
}

TEST_CASE("test_item_parser_durability_default_max") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    // netherite_helmet has max_durability=407
    auto result = ItemParser::parse("netherite_helmet", ench_reg, eq_reg);
    expect(result.durability == 407, "durability defaults to max_durability 407");
    TEST_PASS("test_item_parser_durability_default_max");
}

TEST_CASE("test_item_parser_durability_exceeds_max_throws") {
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

TEST_CASE("test_item_parser_durability_at_max_ok") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("diamond_sword{durability:1561}",
                                    ench_reg, eq_reg);
    expect(result.durability == 1561, "durability at max is allowed");
    TEST_PASS("test_item_parser_durability_at_max_ok");
}

TEST_CASE("test_item_parser_durability_zero_allowed") {
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

TEST_CASE("test_item_parser_both_properties") {
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

TEST_CASE("test_item_parser_properties_only") {
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

TEST_CASE("test_item_parser_missing_brace_throws") {
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

TEST_CASE("test_item_parser_unknown_property_throws") {
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

TEST_CASE("test_item_parser_trailing_after_brace_throws") {
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

TEST_CASE("test_item_parser_negative_prior_penalty_throws") {
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

TEST_CASE("test_item_parser_durability_overflow_throws") {
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

TEST_CASE("test_item_parser_prior_penalty_overflow_throws") {
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

TEST_CASE("test_item_parser_prior_penalty_at_max_ok") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("diamond_sword{prior_penalty:255}",
                                    ench_reg, eq_reg);
    expect(result.prior_penalty == 255, "prior_penalty at max 255 is allowed");
    TEST_PASS("test_item_parser_prior_penalty_at_max_ok");
}

// ============================================================================
// Book targets — a book is not equipment; enchanting it produces an
// enchanted_book (which can hold any enchantment).  `book` therefore
// normalises to `enchanted_book`.
// ============================================================================

TEST_CASE("test_item_parser_book_bare") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("book", ench_reg, eq_reg);
    expect(result.id.str() == "minecraft:enchanted_book",
           "plain book normalises to enchanted_book");
    expect(result.is_book(), "should be flagged as book");
    expect(result.enchantments.empty(), "no enchantments");
    expect(result.durability == 0, "books have no durability");
    expect(result.prior_penalty == 0, "default prior_penalty");
    TEST_PASS("test_item_parser_book_bare");
}

TEST_CASE("test_item_parser_book_with_enchants") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("book[sharpness=5]", ench_reg, eq_reg);
    expect(result.id.str() == "minecraft:enchanted_book",
           "book normalises to enchanted_book when enchanted");
    expect(result.is_book(), "should be flagged as book");
    expect(result.enchantments.size() == 1, "one enchant");
    expect(result.durability == 0, "books have no durability");
    TEST_PASS("test_item_parser_book_with_enchants");
}

TEST_CASE("test_item_parser_enchanted_book_direct") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("enchanted_book[sharpness=5]", ench_reg, eq_reg);
    expect(result.id.str() == "minecraft:enchanted_book",
           "enchanted_book id preserved");
    expect(result.is_book(), "should be flagged as book");
    expect(result.enchantments.size() == 1, "one enchant");
    expect(result.durability == 0, "books have no durability");
    TEST_PASS("test_item_parser_enchanted_book_direct");
}

TEST_CASE("test_item_parser_book_namespaced") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("minecraft:enchanted_book[sharpness=5]",
                                    ench_reg, eq_reg);
    expect(result.id.str() == "minecraft:enchanted_book", "namespaced book ok");
    expect(result.is_book(), "should be flagged as book");
    TEST_PASS("test_item_parser_book_namespaced");
}

TEST_CASE("test_item_parser_book_prior_penalty_ok") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("enchanted_book[sharpness=5]{prior_penalty:2}",
                                    ench_reg, eq_reg);
    expect(result.prior_penalty == 2, "books carry prior penalty");
    expect(result.durability == 0, "books have no durability");
    TEST_PASS("test_item_parser_book_prior_penalty_ok");
}

TEST_CASE("test_item_parser_book_durability_rejected") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        ItemParser::parse("enchanted_book{durability:5}", ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "non-zero durability on a book should throw");
    TEST_PASS("test_item_parser_book_durability_rejected");
}
