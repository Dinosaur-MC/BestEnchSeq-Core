#include "framework/test_utils.h"
#include "parsers/InputParser.h"
#include "cli/cli.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/RegistryAccess.h"
#include "types/EnchInfo.h"
#include "types/Ench.h"
#include "types/EnchSet.h"
#include "types/ItemStack.h"
#include "types/Equipment.h"
#include "registries/EquipmentCategoryRegistry.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

static auto& test_ench_reg = registries::enchants();
static std::unordered_map<std::string, int32_t> ench_id_map;

namespace {

// ---------------------------------------------------------------------------
// Test fixtures
// ---------------------------------------------------------------------------

// Static equipment instances (must outlive all tests)
Equipment diamond_sword{
    "diamond_sword", "Diamond Sword", EquipmentCategory::ID_SWORD, 1561
};
Equipment diamond_pickaxe{
    "diamond_pickaxe", "Diamond Pickaxe", EquipmentCategory::ID_PICKAXE, 1561
};

std::unordered_map<std::string, const Equipment*> test_equipment_registry = {
    {"diamond_sword", &diamond_sword},
    {"diamond_pickaxe", &diamond_pickaxe},
};


void setup_enchinfo() {
    std::vector<EnchInfo> infos;
    infos.reserve(2);
    infos.push_back({
        "minecraft:sharpness",
        "Sharpness",
        MCE::All,
        5,   // max_level
        5,   // limited_level
        1,   // multiplier
        false, // is_treasure
        {},  // exclusive_set
        {EquipmentCategory::ID_SWORD},
    });
    infos.push_back({
        "minecraft:knockback",
        "Knockback",
        MCE::All,
        2,   // max_level
        2,   // limited_level
        2,   // multiplier
        false, // is_treasure
        {},  // exclusive_set
        {EquipmentCategory::ID_SWORD},
    });
    registries::enchants().initialize(infos);
    ench_id_map.clear();
    for (const auto& info : registries::enchants().get_instances())
        ench_id_map[info.name_id] = registries::enchants().get_id(info.name_id);
}

// Helper to create a temporary JSON file
void create_json(const std::string &path, const std::string &content) {
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream f(path);
    f << content;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_parse_inventory_json() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_inv_basic";
    std::filesystem::create_directories(temp_dir);
    auto path = (temp_dir / "test_inv_basic.json").string();
    create_json(path, R"({
        "items": [
            {"type": "book", "enchants": [{"id": "sharpness", "level": 5}], "prior_penalty": 0},
            {"type": "equipment", "id": "diamond_sword", "prior_penalty": 1, "durability": 500},
            {"type": "book", "enchants": [], "prior_penalty": 2}
        ]
    })");

    auto items = InputParser::parse_inventory(path, ench_id_map, test_equipment_registry);

    expect(items.size() == 3, "parse_inventory: expected 3 items");

    // First: book with sharpness 5
    expect(items[0].is_book(), "parse_inventory: item[0] should be a book");
    expect(items[0].enchantments.size() == 1,
           "parse_inventory: item[0] should have 1 enchantment");
    expect(items[0].prior_penalty == 0,
           "parse_inventory: item[0] prior_penalty should be 0");

    // Second: diamond sword with durability 500
    expect(items[1].is_equipment(),
           "parse_inventory: item[1] should be equipment");
    expect(items[1].equipment.has_value(),
           "parse_inventory: item[1] equipment should be present");
    expect(items[1].equipment->name_id == "diamond_sword",
           "parse_inventory: item[1] equipment id should be diamond_sword");
    expect(items[1].prior_penalty == 1,
           "parse_inventory: item[1] prior_penalty should be 1");
    expect(items[1].durability == 500,
           "parse_inventory: item[1] durability should be 500");

    // Third: empty book with prior_penalty 2
    expect(items[2].is_book(), "parse_inventory: item[2] should be a book");
    expect(items[2].enchantments.empty(),
           "parse_inventory: item[2] should have no enchantments");
    expect(items[2].prior_penalty == 2,
           "parse_inventory: item[2] prior_penalty should be 2");

    std::filesystem::remove_all(temp_dir);
    std::cout << "  PASS: test_parse_inventory_json" << std::endl;
}

void test_build_target() {
    TargetSpec spec;
    spec.item_id = "diamond_sword";

    ItemStack target = InputParser::build_target(
        spec, ench_id_map, test_equipment_registry
    );

    expect(target.is_equipment(),
           "build_target: should be equipment");
    expect(target.equipment.has_value(),
           "build_target: equipment should be present");
    expect(target.equipment->name_id == "diamond_sword",
           "build_target: equipment id should be diamond_sword");
    expect(target.enchantments.empty(),
           "build_target: no inline enchants, should be empty");
    expect(target.prior_penalty == 0,
           "build_target: prior_penalty should be 0");

    std::cout << "  PASS: test_build_target" << std::endl;
}

void test_build_target_with_inline() {
    TargetSpec spec;
    spec.item_id = "diamond_sword";
    spec.inline_enchants.push_back({"minecraft", "sharpness", 3});

    ItemStack target = InputParser::build_target(
        spec, ench_id_map, test_equipment_registry
    );

    expect(target.is_equipment(),
           "build_target_inline: should be equipment");
    expect(target.enchantments.size() == 1,
           "build_target_inline: should have 1 enchantment");

    // Find sharpness (id=0) by lookup
    auto it = target.enchantments.find_by_id(0);
    expect(it != target.enchantments.end(),
           "build_target_inline: sharpness should be present");
    expect(it->level == 3,
           "build_target_inline: sharpness level should be 3");

    std::cout << "  PASS: test_build_target_with_inline" << std::endl;
}

void test_build_wanted_enchset() {
    std::vector<EnchantmentSpec> specs;
    specs.push_back({"minecraft", "sharpness", 5});
    specs.push_back({"minecraft", "knockback", 2});

    EnchSet wanted = InputParser::build_wanted_enchset(specs, ench_id_map);

    expect(wanted.size() == 2,
           "build_wanted_enchset: should have 2 enchantments");

    auto it = wanted.find_by_id(0);
    expect(it != wanted.end(),
           "build_wanted_enchset: sharpness should be present");
    expect(it->level == 5,
           "build_wanted_enchset: sharpness level should be 5");

    it = wanted.find_by_id(1);
    expect(it != wanted.end(),
           "build_wanted_enchset: knockback should be present");
    expect(it->level == 2,
           "build_wanted_enchset: knockback level should be 2");

    std::cout << "  PASS: test_build_wanted_enchset" << std::endl;
}

void test_generate_books_auto_complete() {
    // wanted sharpness 5, existing sharpness 3 --> generate upgrade book
    EnchSet wanted;
    wanted.emplace(0, 5);

    EnchSet existing;
    existing.emplace(0, 3);

    auto books = InputParser::generate_books(
        wanted, existing
    );

    // Graduated book generation: levels existing+1 .. wanted
    expect(books.size() == 2,
           "generate_books: should generate 2 graduated books (4,5)");
    expect(books[0].is_book(),
           "generate_books: should be a book");
    expect(books[0].enchantments.size() == 1,
           "generate_books: book should have 1 enchantment");

    // First book should be at level 4 (existing+1)
    auto it = books[0].enchantments.find_by_id(0);
    expect(it != books[0].enchantments.end(),
           "generate_books: first book should have sharpness");
    expect(it->level == 4,
           "generate_books: first book level should be 4 (existing+1)");

    // Second book should be at level 5 (wanted level)
    auto it2 = books[1].enchantments.find_by_id(0);
    expect(it2 != books[1].enchantments.end(),
           "generate_books: second book should have sharpness");
    expect(it2->level == 5,
           "generate_books: second book level should be 5 (wanted level)");

    // Test case where existing has same level --> skip
    EnchSet existing_same;
    existing_same.emplace(0, 5);
    auto books_same = InputParser::generate_books(
        wanted, existing_same
    );
    expect(books_same.empty(),
           "generate_books: no book when existing has same level");

    // Test case where existing has higher level --> skip
    EnchSet existing_higher;
    existing_higher.emplace(0, 7);
    auto books_higher = InputParser::generate_books(
        wanted, existing_higher
    );
    expect(books_higher.empty(),
           "generate_books: no book when existing has higher level");

    // Test case where wanted is not in existing at all --> generate
    EnchSet existing_empty;
    auto books_missing = InputParser::generate_books(
        wanted, existing_empty
    );
    expect(books_missing.size() == 5,
           "generate_books: should generate 5 graduated books (1..5) when missing entirely");

    std::cout << "  PASS: test_generate_books_auto_complete" << std::endl;
}

void test_assemble_input_direct_mode() {
    CLIConfig config;
    config.mode = "direct";
    config.target = "diamond_sword";
    config.wanted = "sharpness=5,knockback=2";
    config.platform = "auto";

    auto input = InputParser::assemble_input(
        config, ench_id_map, test_equipment_registry
    );

    expect(input.platform == MCE::All,
           "assemble_input: platform should be All");

    expect(input.target_item.is_equipment(),
           "assemble_input: target should be equipment");
    expect(input.target_item.equipment->name_id == "diamond_sword",
           "assemble_input: target id should be diamond_sword");
    expect(input.target_item.enchantments.empty(),
           "assemble_input: target should have no inline enchantments");
    expect(input.source_ench.empty(),
           "assemble_input: source_ench should be same as target enchants");

    // Two wanted enchants: sharpness=5 -> 5 graduated books, knockback=2 -> 2 graduated books
    expect(input.available_items.size() == 7,
           "assemble_input: should have 7 graduated books for 2 new enchants");
    for (const auto &item : input.available_items) {
        expect(item.is_book(),
               "assemble_input: available item should be a book");
    }

    std::cout << "  PASS: test_assemble_input_direct_mode" << std::endl;
}

void test_inventory_missing_type_field() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_inv_no_type";
    std::filesystem::create_directories(temp_dir);
    auto path = (temp_dir / "test_inv_no_type.json").string();
    create_json(path, R"({
        "items": [
            {"enchants": [{"id": "sharpness", "level": 5}], "prior_penalty": 1},
            {"type": "book", "enchants": [], "prior_penalty": 0}
        ]
    })");

    auto items = InputParser::parse_inventory(path, ench_id_map, test_equipment_registry);

    expect(items.size() == 1,
           "inventory_missing_type: only the explicit book should be parsed");

    std::filesystem::remove_all(temp_dir);
    std::cout << "  PASS: test_inventory_missing_type_field" << std::endl;
}

void test_empty_inventory() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_inv_empty";
    std::filesystem::create_directories(temp_dir);
    auto path = (temp_dir / "test_inv_empty.json").string();
    create_json(path, R"({
        "items": []
    })");

    auto items = InputParser::parse_inventory(path, ench_id_map, test_equipment_registry);

    expect(items.empty(),
           "empty_inventory: should have no items");

    std::filesystem::remove_all(temp_dir);
    std::cout << "  PASS: test_empty_inventory" << std::endl;
}

// ---------------------------------------------------------------------------
// Boundary tests (3.8)
// ---------------------------------------------------------------------------

void test_inventory_negative_ppn() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_neg_ppn";
    std::filesystem::create_directories(temp_dir);
    auto path = (temp_dir / "test_neg_ppn.json").string();
    create_json(path, R"({
        "items": [
            {"type": "book", "enchants": [{"id": "sharpness", "level": 5}], "prior_penalty": -3}
        ]
    })");

    bool threw = false;
    try {
        auto items = InputParser::parse_inventory(path, ench_id_map, test_equipment_registry);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "negative_ppn: should throw on negative prior_penalty");

    std::filesystem::remove_all(temp_dir);
    std::cout << "  PASS: test_inventory_negative_ppn" << std::endl;
}

void test_inventory_durability_exceeds_max() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_durability_exceeds";
    std::filesystem::create_directories(temp_dir);
    auto path = (temp_dir / "test_durability_exceeds.json").string();
    create_json(path, R"({
        "items": [
            {"type": "equipment", "id": "diamond_sword", "durability": 9999, "prior_penalty": 0}
        ]
    })");

    auto items = InputParser::parse_inventory(path, ench_id_map, test_equipment_registry);

    expect(items.size() == 1, "durability_exceeds: should parse 1 item");
    expect(items[0].durability == 9999,
           "durability_exceeds: durability should be 9999 as provided");

    std::filesystem::remove_all(temp_dir);
    std::cout << "  PASS: test_inventory_durability_exceeds_max" << std::endl;
}

void test_inventory_multi_ench_book() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_multi_ench";
    std::filesystem::create_directories(temp_dir);
    auto path = (temp_dir / "test_multi_ench.json").string();
    create_json(path, R"({
        "items": [
            {"type": "book", "enchants": [{"id": "sharpness", "level": 5}, {"id": "knockback", "level": 2}], "prior_penalty": 0}
        ]
    })");

    auto items = InputParser::parse_inventory(path, ench_id_map, test_equipment_registry);

    expect(items.size() == 1, "multi_ench_book: should parse 1 item");
    expect(items[0].is_book(), "multi_ench_book: should be a book");
    expect(items[0].enchantments.size() == 2,
           "multi_ench_book: should have 2 enchantments");

    auto it = items[0].enchantments.find_by_id(0);
    expect(it != items[0].enchantments.end(),
           "multi_ench_book: should contain sharpness");
    expect(it->level == 5,
           "multi_ench_book: sharpness level should be 5");

    it = items[0].enchantments.find_by_id(1);
    expect(it != items[0].enchantments.end(),
           "multi_ench_book: should contain knockback");
    expect(it->level == 2,
           "multi_ench_book: knockback level should be 2");

    std::filesystem::remove_all(temp_dir);
    std::cout << "  PASS: test_inventory_multi_ench_book" << std::endl;
}

} // anonymous namespace

// ===========================================================================
int main() {
    std::cout << "=== InputParser Tests ===" << std::endl;

    try {
        setup_enchinfo();

        test_parse_inventory_json();
        test_build_target();
        test_build_target_with_inline();
        test_build_wanted_enchset();
        test_generate_books_auto_complete();
        test_assemble_input_direct_mode();
        test_inventory_missing_type_field();
        test_empty_inventory();
        test_inventory_negative_ppn();
        test_inventory_durability_exceeds_max();
        test_inventory_multi_ench_book();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
