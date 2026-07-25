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
    TEST_PASS("test_item_parser_bare");
}

void test_item_parser_with_enchants() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto result = ItemParser::parse("diamond_sword[sharpness=5,knockback=2]",
                                    ench_reg, eq_reg);
    expect(!result.id.str().empty(), "id should be set");
    expect(result.enchantments.size() == 2, "should have 2 enchants");
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

int main() {
    try {
        test_item_parser_bare();
        test_item_parser_with_enchants();
        test_item_parser_no_bracket_close();
        test_item_parser_trailing_content();
        test_item_parser_unknown_equip_throws();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
