#include "framework/test_utils.h"
#include "domain/interface/cli/InventoryParser.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include <filesystem>
#include <fstream>

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

// Unique temp file path for an inventory JSON payload.
static std::string write_temp(const std::string& content) {
    static int counter = 0;
    auto path = std::filesystem::temp_directory_path() /
                ("besq_inv_test_" + std::to_string(++counter) + ".json");
    std::ofstream f(path);
    f << content;
    return path.string();
}

// ============================================================================
// Tests
// ============================================================================

void test_parse_valid() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto path = write_temp(R"({
        "items": [
            { "type": "book", "enchants": [{"id":"sharpness","level":5}],
              "prior_penalty": 0, "priority": 1 },
            { "type": "equipment", "id": "diamond_sword", "enchants": [],
              "prior_penalty": 2, "durability": 500, "priority": 10 }
        ]
    })");
    auto inv = InventoryParser::parse_file(path, ench_reg, eq_reg);

    expect(inv.items.size() == 2, "two items parsed");
    expect(inv.items[0].is_book(), "first item is a book");
    expect(inv.items[0].enchantments.size() == 1, "book has 1 enchant");
    expect(inv.items[0].prior_penalty == 0, "book prior_penalty");
    expect(inv.items[1].id == NSID("minecraft:diamond_sword"),
           "second item is the diamond sword");
    expect(inv.items[1].prior_penalty == 2, "equipment prior_penalty");
    expect(inv.items[1].durability == 500, "explicit durability respected");
    expect(inv.priorities.size() == 2, "two priorities");
    expect(inv.priorities[0] == 1, "book priority 1");
    expect(inv.priorities[1] == 10, "equipment priority 10");

    TEST_PASS("test_parse_valid");
}

void test_parse_empty_items() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto path = write_temp(R"({ "items": [] })");
    auto inv = InventoryParser::parse_file(path, ench_reg, eq_reg);
    expect(inv.items.empty(), "empty items list yields empty result");
    TEST_PASS("test_parse_empty_items");
}

void test_unknown_equipment_throws() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto path = write_temp(R"({ "items": [
        { "type": "equipment", "id": "not_a_real_sword", "enchants": [] }
    ] })");
    bool threw = false;
    try {
        InventoryParser::parse_file(path, ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "unknown equipment id should throw");
    TEST_PASS("test_unknown_equipment_throws");
}

void test_unknown_ench_throws() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto path = write_temp(R"({ "items": [
        { "type": "book", "enchants": [{"id":"nonexistent","level":1}] }
    ] })");
    bool threw = false;
    try {
        InventoryParser::parse_file(path, ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "unknown enchantment should throw");
    TEST_PASS("test_unknown_ench_throws");
}

void test_book_over_level_ench_throws() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto path = write_temp(R"({ "items": [
        { "type": "book", "enchants": [{"id":"sharpness","level":10}] }
    ] })");  // sharpness max_level is 5
    bool threw = false;
    try {
        InventoryParser::parse_file(path, ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "book enchantment level exceeding max should throw");
    TEST_PASS("test_book_over_level_ench_throws");
}

void test_equipment_over_level_ench_throws() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto path = write_temp(R"({ "items": [
        { "type": "equipment", "id": "diamond_sword",
          "enchants": [{"id":"knockback","level":9}] }
    ] })");  // knockback max_level is 2
    bool threw = false;
    try {
        InventoryParser::parse_file(path, ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "equipment enchantment level exceeding max should throw");
    TEST_PASS("test_equipment_over_level_ench_throws");
}

void test_bad_type_throws() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto path = write_temp(R"({ "items": [
        { "type": "sword", "enchants": [] }
    ] })");
    bool threw = false;
    try {
        InventoryParser::parse_file(path, ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "invalid item type should throw");
    TEST_PASS("test_bad_type_throws");
}

void test_missing_file_throws() {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        InventoryParser::parse_file("no_such_inventory_file.json", ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "missing file should throw");
    TEST_PASS("test_missing_file_throws");
}

// ============================================================================
// main
// ============================================================================
int main() {
    try {
        test_parse_valid();
        test_parse_empty_items();
        test_unknown_equipment_throws();
        test_unknown_ench_throws();
        test_book_over_level_ench_throws();
        test_equipment_over_level_ench_throws();
        test_bad_type_throws();
        test_missing_file_throws();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
