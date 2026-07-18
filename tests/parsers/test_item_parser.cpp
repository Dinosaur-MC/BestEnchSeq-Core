#include "framework/test_utils.h"
#include "parsers/ItemParser.h"

void test_item_parser_bare() {
    auto result = ItemParser::parse("diamond_sword");
    expect_eq(result.item_id, "diamond_sword", "bare item id");
    expect(result.inline_enchants.empty(), "no inline enchants");
    TEST_PASS("test_item_parser_bare");
}

void test_item_parser_with_enchants() {
    auto result = ItemParser::parse("diamond_sword[sharpness=5,knockback=2]");
    expect_eq(result.item_id, "diamond_sword", "item id");
    expect(result.inline_enchants.size() == 2, "should have 2 enchants");
    if (result.inline_enchants.size() >= 2) {
        expect_eq(result.inline_enchants[0].id, "sharpness", "first enchant id");
        expect_eq(result.inline_enchants[0].level, 5, "first enchant level");
        expect_eq(result.inline_enchants[1].id, "knockback", "second enchant id");
        expect_eq(result.inline_enchants[1].level, 2, "second enchant level");
    }
    TEST_PASS("test_item_parser_with_enchants");
}

void test_item_parser_no_bracket_close() {
    bool threw = false;
    try {
        ItemParser::parse("sword[sharpness=5");
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "missing closing bracket should throw");
    TEST_PASS("test_item_parser_no_bracket_close");
}

void test_item_parser_trailing_content() {
    bool threw = false;
    try {
        ItemParser::parse("sword[a=1][b=2]");
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "second bracket group should throw warning");
    TEST_PASS("test_item_parser_trailing_content");
}

int main() {
    try {
        test_item_parser_bare();
        test_item_parser_with_enchants();
        test_item_parser_no_bracket_close();
        test_item_parser_trailing_content();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
