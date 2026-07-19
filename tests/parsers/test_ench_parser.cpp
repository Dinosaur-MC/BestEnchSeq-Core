#include "framework/test_utils.h"
#include "parsers/EnchParser.h"

// ============================================================================
// Basic "id=level" syntax
// ============================================================================
void test_ench_parser_simple() {
    auto result = EnchParser::parse("sharpness=5");
    expect(result.size() == 1, "should parse one enchantment");
    if (!result.empty()) {
        expect_eq(result[0].ns, "minecraft", "default namespace");
        expect_eq(result[0].id, "sharpness", "id");
        expect_eq(result[0].level, 5, "level");
    }
    TEST_PASS("test_ench_parser_simple");
}

// ============================================================================
// Namespaced: "ns:id=level" and comma-separated
// ============================================================================
void test_ench_parser_namespaced() {
    auto result = EnchParser::parse("minecraft:sharpness=5,modded:foo=3");
    expect(result.size() == 2, "should parse two enchantments");
    if (result.size() >= 2) {
        expect_eq(result[0].ns, std::string("minecraft"), "first ns");
        expect_eq(result[0].id, std::string("sharpness"), "first id");
        expect_eq(result[0].level, 5, "first level");
        expect_eq(result[1].ns, std::string("modded"), "second ns");
        expect_eq(result[1].id, std::string("foo"), "second id");
        expect_eq(result[1].level, 3, "second level");
    }
    TEST_PASS("test_ench_parser_namespaced");
}

// ============================================================================
// Colon shorthand: "id:level"
// ============================================================================
void test_ench_parser_colon_shorthand() {
    auto result = EnchParser::parse("sharpness:5");
    expect(result.size() == 1, "should parse one");
    if (!result.empty()) {
        expect_eq(result[0].ns, std::string("minecraft"), "ns");
        expect_eq(result[0].id, std::string("sharpness"), "id");
        expect_eq(result[0].level, 5, "level");
    }
    TEST_PASS("test_ench_parser_colon_shorthand");
}

// ============================================================================
// No level specified — defaults to 1
// ============================================================================
void test_ench_parser_no_level() {
    auto result = EnchParser::parse("sharpness");
    if (!result.empty()) {
        expect_eq(result[0].level, 1, "default level should be 1");
    }
    TEST_PASS("test_ench_parser_no_level");
}

// ============================================================================
// Negative level rejected
// ============================================================================
void test_ench_parser_negative_level_rejected() {
    bool threw = false;
    try {
        EnchParser::parse("sharpness=-5");
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
    bool threw = false;
    try {
        EnchParser::parse("=5");
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
    bool threw = false;
    try {
        EnchParser::parse("a=1,,b=2");
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "empty token (double comma) should throw");
    TEST_PASS("test_ench_parser_empty_token_rejected");
}

void test_ench_parser_level_too_high_rejected() {
    bool threw = false;
    try {
        EnchParser::parse("sharpness=256");
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "level > 255 should throw");
    TEST_PASS("test_ench_parser_level_too_high_rejected");
}

// ============================================================================
// main
// ============================================================================
int main() {
    try {
        test_ench_parser_simple();
        test_ench_parser_namespaced();
        test_ench_parser_colon_shorthand();
        test_ench_parser_no_level();
        test_ench_parser_negative_level_rejected();
        test_ench_parser_empty_id_rejected();
        test_ench_parser_empty_token_rejected();
        test_ench_parser_level_too_high_rejected();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
