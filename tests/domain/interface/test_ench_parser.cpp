#include "framework/test_utils.h"
#include "domain/interface/cli/EnchParser.h"
#include "domain/business/registries/EnchantmentRegistry.h"

// ============================================================================
// Helper: minimal registry with common enchantments
// ============================================================================
static EnchantmentRegistry make_test_registry() {
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
// Basic "id=level" syntax
// ============================================================================
void test_ench_parser_simple() {
    auto reg = make_test_registry();
    auto result = EnchParser::parse("sharpness=5", reg);
    expect(result.size() == 1, "should parse one enchantment");
    if (!result.empty()) {
        auto& ench = *result.begin();
        expect(ench.level == 5, "level should be 5");
    }
    TEST_PASS("test_ench_parser_simple");
}

// ============================================================================
// Multiple enchantments (comma-separated)
// ============================================================================
void test_ench_parser_multiple() {
    auto reg = make_test_registry();
    auto result = EnchParser::parse("sharpness=5,knockback=2", reg);
    expect(result.size() == 2, "should parse two enchantments");
    TEST_PASS("test_ench_parser_multiple");
}

// ============================================================================
// Colon shorthand: "id:level"
// ============================================================================
void test_ench_parser_colon_shorthand() {
    auto reg = make_test_registry();
    auto result = EnchParser::parse("sharpness:5", reg);
    expect(result.size() == 1, "should parse one");
    if (!result.empty()) {
        auto& ench = *result.begin();
        expect(ench.level == 5, "level should be 5");
    }
    TEST_PASS("test_ench_parser_colon_shorthand");
}

// ============================================================================
// Negative level rejected
// ============================================================================
void test_ench_parser_negative_level_rejected() {
    EnchantmentRegistry empty_reg;
    bool threw = false;
    try {
        EnchParser::parse("sharpness=-5", empty_reg);
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "negative level should throw");
    TEST_PASS("test_ench_parser_negative_level_rejected");
}

// ============================================================================
// Empty id rejected (e.g. "=5")
// ============================================================================
void test_ench_parser_empty_id_rejected() {
    EnchantmentRegistry empty_reg;
    bool threw = false;
    try {
        EnchParser::parse("=5", empty_reg);
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "empty id should throw");
    TEST_PASS("test_ench_parser_empty_id_rejected");
}

// ============================================================================
// Empty token between commas is rejected
// ============================================================================
void test_ench_parser_empty_token_rejected() {
    EnchantmentRegistry empty_reg;
    bool threw = false;
    try {
        EnchParser::parse("a=1,,b=2", empty_reg);
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "empty token (double comma) should throw");
    TEST_PASS("test_ench_parser_empty_token_rejected");
}

void test_ench_parser_level_too_high_rejected() {
    EnchantmentRegistry empty_reg;
    bool threw = false;
    try {
        EnchParser::parse("sharpness=256", empty_reg);
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "level > 255 should throw");
    TEST_PASS("test_ench_parser_level_too_high_rejected");
}

// ============================================================================
// Unknown enchantment throws
// ============================================================================
void test_ench_parser_blank_input_rejected() {
    EnchantmentRegistry empty_reg;
    bool threw = false;
    try {
        EnchParser::parse("   ", empty_reg);
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "whitespace-only input should throw");
    TEST_PASS("test_ench_parser_blank_input_rejected");
}

void test_ench_parser_trailing_comma_rejected() {
    EnchantmentRegistry empty_reg;
    bool threw = false;
    try {
        EnchParser::parse("sharpness=5,", empty_reg);
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "trailing comma should throw");
    TEST_PASS("test_ench_parser_trailing_comma_rejected");
}

void test_ench_parser_unknown_throws() {
    auto reg = make_test_registry();
    bool threw = false;
    try {
        EnchParser::parse("nonexistent=1", reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "unknown enchantment should throw");
    TEST_PASS("test_ench_parser_unknown_throws");
}

// ============================================================================
// Invalid NSID input maps to the friendly unknown-enchantment error (#22)
//
// NSID validation rejects uppercase / `.`/`..` segments with the bare validator
// text "The NSID '...' is invalid".  The parser must instead surface the
// actionable cli.err.unknown_ench error.
// ============================================================================

void test_ench_parser_uppercase_maps_to_unknown() {
    auto reg = make_test_registry();
    bool threw = false;
    try {
        EnchParser::parse("Sharpness=5", reg);  // uppercase → invalid NSID
    } catch (const std::exception& e) {
        threw = true;
        std::string msg(e.what());
        expect(msg.find("The NSID") == std::string::npos,
               "raw NSID validator text must not surface");
        expect(msg.find("cli.err.unknown_ench") != std::string::npos ||
                   msg.find("Unknown enchantment") != std::string::npos,
               "uppercase id maps to friendly unknown-enchantment error");
    }
    expect(threw, "uppercase enchantment id should throw");
    TEST_PASS("test_ench_parser_uppercase_maps_to_unknown");
}

void test_ench_parser_dot_segment_maps_to_unknown() {
    auto reg = make_test_registry();
    bool threw = false;
    try {
        EnchParser::parse("minecraft:..=3", reg);  // `..` segment → invalid NSID
    } catch (const std::exception& e) {
        threw = true;
        std::string msg(e.what());
        expect(msg.find("The NSID") == std::string::npos,
               "raw NSID validator text must not surface");
        expect(msg.find("cli.err.unknown_ench") != std::string::npos ||
                   msg.find("Unknown enchantment") != std::string::npos,
               "dot-segment id maps to friendly unknown-enchantment error");
    }
    expect(threw, "dot-segment enchantment id should throw");
    TEST_PASS("test_ench_parser_dot_segment_maps_to_unknown");
}

void test_ench_parser_duplicate_rejected() {
    auto reg = make_test_registry();
    bool threw = false;
    try {
        EnchParser::parse("sharpness=5,sharpness=3", reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "duplicate enchantment should throw");
    TEST_PASS("test_ench_parser_duplicate_rejected");
}

// ============================================================================
// main
// ============================================================================
int main() {
    try {
        test_ench_parser_simple();
        test_ench_parser_multiple();
        test_ench_parser_colon_shorthand();
        test_ench_parser_negative_level_rejected();
        test_ench_parser_empty_id_rejected();
        test_ench_parser_empty_token_rejected();
        test_ench_parser_level_too_high_rejected();
        test_ench_parser_unknown_throws();
        test_ench_parser_uppercase_maps_to_unknown();
        test_ench_parser_dot_segment_maps_to_unknown();
        test_ench_parser_blank_input_rejected();
        test_ench_parser_trailing_comma_rejected();
        test_ench_parser_duplicate_rejected();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
