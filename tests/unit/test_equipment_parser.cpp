#include "framework/test_utils.h"
#include "parser/EquipmentParser.h"
#include "parser/TagResolver.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/RegistryAccess.h"
#include <iostream>
#include <fstream>
#include <filesystem>

static auto& test_cat_reg = registries::categories();

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void create_file(const std::string &path, const std::string &content) {
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream f(path);
    f << content;
}

// ---------------------------------------------------------------------------
// test_json_basic
// ---------------------------------------------------------------------------
void test_json_basic() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_json";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_eq_json.json").string();
    create_file(file, R"({
        "equipments": [
            {"id": "diamond_sword", "name": "Diamond Sword", "category": "sword", "max_durability": 1561},
            {"id": "diamond_pickaxe", "name": "Diamond Pickaxe", "category": "pickaxe", "max_durability": 1561}
        ]
    })");

    TagResolver resolver;
    auto eqs = EquipmentParser::parse_native_json(file, resolver, test_cat_reg);

    expect(eqs.size() == 2, "json: 2 equipment");
    expect(eqs[0].name_id == "diamond_sword", "json: first id");
    expect(eqs[0].name == "Diamond Sword", "json: first name");
    expect(eqs[0].category_id == EquipmentCategory::ID_SWORD, "json: sword category");
    expect(eqs[0].max_durability == 1561, "json: first durability");

    expect(eqs[1].name_id == "diamond_pickaxe", "json: second id");
    expect(eqs[1].name == "Diamond Pickaxe", "json: second name");
    expect(eqs[1].category_id == EquipmentCategory::ID_PICKAXE, "json: pickaxe category");
    expect(eqs[1].max_durability == 1561, "json: second durability");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_csv_basic
// ---------------------------------------------------------------------------
void test_csv_basic() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_csv";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_eq_csv.csv").string();
    {
        std::ofstream f(file);
        f << "id,name,category,max_durability\n";
        f << "diamond_sword,Diamond Sword,sword,1561\n";
        f << "diamond_pickaxe,Diamond Pickaxe,pickaxe,1561\n";
    }

    auto eqs = EquipmentParser::parse_native_csv(file, test_cat_reg);

    expect(eqs.size() == 2, "csv: 2 equipment");
    expect(eqs[0].name_id == "diamond_sword", "csv: first id");
    expect(eqs[0].name == "Diamond Sword", "csv: first name");
    expect(eqs[0].category_id == EquipmentCategory::ID_SWORD, "csv: first category");
    expect(eqs[0].max_durability == 1561, "csv: first durability");

    expect(eqs[1].name_id == "diamond_pickaxe", "csv: second id");
    expect(eqs[1].max_durability == 1561, "csv: second durability");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_csv_no_durability
// ---------------------------------------------------------------------------
void test_csv_no_durability() {
    // max_durability column is optional; missing should default to 0
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_csv_no_durability";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_eq_csv_no_durability.csv").string();
    {
        std::ofstream f(file);
        f << "id,name,category\n";
        f << "diamond_sword,Diamond Sword,sword\n";
    }

    auto eqs = EquipmentParser::parse_native_csv(file, test_cat_reg);
    expect(eqs.size() == 1, "csv no durability: parsed");
    expect(eqs[0].max_durability == 0, "csv no durability: defaults to 0");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_custom_category
// ---------------------------------------------------------------------------
void test_custom_category() {
    // Equipment with custom/unknown category should still work
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_custom_cat";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_eq_custom_cat.json").string();
    create_file(file, R"({
        "equipments": [
            {"id": "custom_weapon", "name": "Custom Weapon", "category": "custom_weapon", "max_durability": 500}
        ]
    })");

    TagResolver resolver;
    auto eqs = EquipmentParser::parse_native_json(file, resolver, test_cat_reg);

    expect(eqs.size() == 1, "custom cat: parsed");
    expect(eqs[0].name_id == "custom_weapon", "custom cat: id");
    expect(eqs[0].category_id == EquipmentCategory::ID_ANY, "custom cat: category preserved (falls back to ID_ANY)");
    expect(eqs[0].max_durability == 500, "custom cat: durability");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_missing_fields_skipped
// ---------------------------------------------------------------------------
void test_missing_fields_skipped() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_missing";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_eq_missing.json").string();
    create_file(file, R"({
        "equipments": [
            {"id": "valid", "category": "sword", "max_durability": 100},
            {"category": "axe"},                        {"id": "no_category"},                         {"id": "", "category": "sword"},
            {"id": "no_name", "category": "pickaxe"}
        ]
    })");

    TagResolver resolver;
    auto eqs = EquipmentParser::parse_native_json(file, resolver, test_cat_reg);

    // Only the first and last should be valid
    expect(eqs.size() == 2, "missing fields: 2 valid entries");

    // Find by id
    bool found_valid  = false;
    bool found_noname = false;
    for (const auto &eq : eqs) {
        if (eq.name_id == "valid") {
            found_valid = true;
            expect(eq.name == "valid", "missing fields: name fallback to id");
            expect(eq.category_id == EquipmentCategory::ID_SWORD, "missing fields: valid category");
            expect(eq.max_durability == 100, "missing fields: valid durability");
        }
        if (eq.name_id == "no_name") {
            found_noname = true;
            expect(eq.name == "no_name", "missing fields: no_name fallback");
            expect(eq.category_id == EquipmentCategory::ID_PICKAXE, "missing fields: pickaxe category");
            expect(eq.max_durability == 0, "missing fields: no durability defaults to 0");
        }
    }
    expect(found_valid, "missing fields: 'valid' found");
    expect(found_noname, "missing fields: 'no_name' found");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_empty_equipments
// ---------------------------------------------------------------------------
void test_empty_equipments() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_empty";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_eq_empty.json").string();
    create_file(file, R"({
        "equipments": []
    })");

    TagResolver resolver;
    auto eqs = EquipmentParser::parse_native_json(file, resolver, test_cat_reg);

    expect(eqs.empty(), "empty equipments array: empty result");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_missing_equipments_key
// ---------------------------------------------------------------------------
void test_missing_equipments_key() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_no_key";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_eq_no_key.json").string();
    create_file(file, R"({
        "enchantments": []
    })");

    TagResolver resolver;
    auto eqs = EquipmentParser::parse_native_json(file, resolver, test_cat_reg);

    expect(eqs.empty(), "missing equipments key: empty result");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_json_mixed_with_enchantments
// ---------------------------------------------------------------------------
void test_json_mixed_with_enchantments() {
    // The native JSON combines both enchantments and equipments in one file
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_mixed";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_eq_mixed.json").string();
    create_file(file, R"({
        "enchantments": [
            {"id": "sharpness", "max_level": 5, "multiplier": 1}
        ],
        "equipments": [
            {"id": "diamond_sword", "name": "Diamond Sword", "category": "sword", "max_durability": 1561}
        ]
    })");

    TagResolver resolver;
    auto eqs = EquipmentParser::parse_native_json(file, resolver, test_cat_reg);

    expect(eqs.size() == 1, "mixed json: 1 equipment");
    expect(eqs[0].name_id == "diamond_sword", "mixed json: id");
    expect(eqs[0].category_id == EquipmentCategory::ID_SWORD, "mixed json: category");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_csv_missing_required_columns
// ---------------------------------------------------------------------------
void test_csv_missing_required_columns() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_csv_missing_cols";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_eq_csv_missing_cols.csv").string();
    {
        std::ofstream f(file);
        f << "id,name\n";
        f << "diamond_sword,Diamond Sword\n";
    }

    auto eqs = EquipmentParser::parse_native_csv(file, test_cat_reg);
    expect(eqs.empty(), "csv missing required columns: empty result");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_csv_empty_file
// ---------------------------------------------------------------------------
void test_csv_empty_file() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_csv_empty";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_eq_csv_empty.csv").string();
    {
        std::ofstream f(file);
        f << "id,name,category,max_durability\n";
    }

    auto eqs = EquipmentParser::parse_native_csv(file, test_cat_reg);
    expect(eqs.empty(), "csv with only header: empty result");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_parse_auto_detect_json
// ---------------------------------------------------------------------------
void test_parse_auto_detect_json() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_auto";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_eq_auto.json").string();
    create_file(file, R"({
        "equipments": [
            {"id": "item_a", "category": "sword", "max_durability": 100},
            {"id": "item_b", "category": "pickaxe", "max_durability": 200}
        ]
    })");

    TagResolver resolver;
    auto eqs = EquipmentParser::parse(file, resolver, test_cat_reg);

    expect(eqs.size() == 2, "auto-detect json: 2 equipment");
    expect(eqs[0].name_id == "item_a" || eqs[0].name_id == "item_b", "auto-detect json: valid id");
    expect(eqs[0].max_durability == 100 || eqs[0].max_durability == 200,
           "auto-detect json: valid durability");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_parse_auto_detect_csv
// ---------------------------------------------------------------------------
void test_parse_auto_detect_csv() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_auto_csv";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_eq_auto.csv").string();
    {
        std::ofstream f(file);
        f << "id,name,category,max_durability\n";
        f << "diamond_sword,Diamond Sword,sword,1561\n";
    }

    TagResolver resolver;
    auto eqs = EquipmentParser::parse(file, resolver, test_cat_reg);

    expect(eqs.size() == 1, "auto-detect csv: 1 equipment");
    expect(eqs[0].name_id == "diamond_sword", "auto-detect csv: id");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_parse_mc_official_stub
// ---------------------------------------------------------------------------
void test_parse_mc_official_stub() {
    // MC official parsing should handle empty/non-existent directories gracefully
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_mc_off_empty";
    std::filesystem::create_directories(temp_dir);
    auto dir = (temp_dir / "test_eq_mc_off_empty").string();

    auto eqs = EquipmentParser::parse_mc_official(dir, test_cat_reg);
    expect(eqs.empty(), "mc official empty dir: empty result");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_parse_mc_official_with_items
// ---------------------------------------------------------------------------
void test_parse_mc_official_with_items() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_mc_off_items";
    std::filesystem::create_directories(temp_dir);
    auto dir = (temp_dir / "test_eq_mc_off_items").string();
    std::filesystem::create_directories(dir + "/data/minecraft/items");

    create_file(dir + "/data/minecraft/items/diamond_sword.json", R"({
        "components": {
            "max_damage": 1561
        }
    })");

    create_file(dir + "/data/minecraft/items/diamond_pickaxe.json", R"({
        "components": {
            "max_damage": 1561
        }
    })");

    // Non-item file (no .json extension) should be skipped
    create_file(dir + "/data/minecraft/items/readme.txt", "not an item");

    TagResolver resolver;
    auto eqs = EquipmentParser::parse_mc_official(dir, test_cat_reg);

    expect(eqs.size() == 2, "mc official items: 2 equipment");
    expect(eqs[0].name_id == "minecraft:diamond_sword" || eqs[0].name_id == "minecraft:diamond_pickaxe",
           "mc official items: namespaced id");
    expect(eqs[0].max_durability == 1561, "mc official items: durability from max_damage");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_parse_mc_official_no_items_dir
// ---------------------------------------------------------------------------
void test_parse_mc_official_no_items_dir() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_mc_off_no_items";
    std::filesystem::create_directories(temp_dir);
    auto dir = (temp_dir / "test_eq_mc_off_no_items").string();
    std::filesystem::create_directories(dir + "/data/minecraft");

    auto eqs = EquipmentParser::parse_mc_official(dir, test_cat_reg);
    expect(eqs.empty(), "mc official no items dir: empty result");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_invalid_json_handling
// ---------------------------------------------------------------------------
void test_invalid_json_handling() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_invalid";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_eq_invalid.json").string();
    {
        std::ofstream f(file);
        f << "not valid json";
    }

    TagResolver resolver;
    auto eqs = EquipmentParser::parse_native_json(file, resolver, test_cat_reg);
    expect(eqs.empty(), "invalid json: empty result");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_non_existent_file
// ---------------------------------------------------------------------------
void test_non_existent_file() {
    TagResolver resolver;
    auto eqs = EquipmentParser::parse_native_json("nonexistent.json", resolver, test_cat_reg);
    expect(eqs.empty(), "non-existent file: empty result");
}

// ---------------------------------------------------------------------------
// test_parse_with_invalid_format
// ---------------------------------------------------------------------------
void test_parse_with_invalid_format() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_eq_invalid_format";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_eq_invalid_format.xyz").string();
    {
        std::ofstream f(file);
        f << "some content";
    }

    TagResolver resolver;
    bool threw = false;
    try {
        EquipmentParser::parse(file, resolver, test_cat_reg);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    expect(threw, "invalid format: should throw runtime_error");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_to_json_round_trip
// ---------------------------------------------------------------------------
void test_to_json_round_trip() {
    std::vector<Equipment> original = {
        {"diamond_sword", "Diamond Sword", EquipmentCategory::ID_SWORD, 1561},
        {"diamond_pickaxe", "Diamond Pickaxe", EquipmentCategory::ID_PICKAXE, 1561}
    };

    // Serialize to JSON
    std::string json_str = EquipmentParser::to_json(original, test_cat_reg);

    // Write to temp file, parse back
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_rt_eq_json";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_rt_eq.json").string();
    {
        std::ofstream f(file);
        f << json_str;
    }

    TagResolver resolver;
    auto parsed = EquipmentParser::parse_native_json(file, resolver, test_cat_reg);

    expect(parsed.size() == original.size(), "eq JSON round-trip: same count");
    if (parsed.size() >= 1) {
        expect(parsed[0].name_id == original[0].name_id, "eq JSON round-trip: id preserved");
        expect(parsed[0].name == original[0].name, "eq JSON round-trip: name preserved");
        expect(parsed[0].category_id == EquipmentCategory::ID_SWORD, "eq JSON round-trip: category");
        expect(parsed[0].max_durability == 1561, "eq JSON round-trip: durability");
    }

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_to_csv_round_trip
// ---------------------------------------------------------------------------
void test_to_csv_round_trip() {
    std::vector<Equipment> original = {
        {"diamond_sword", "Diamond Sword", EquipmentCategory::ID_SWORD, 1561},
        {"diamond_pickaxe", "Diamond Pickaxe", EquipmentCategory::ID_PICKAXE, 1561}
    };

    // Serialize to CSV
    std::string csv_str = EquipmentParser::to_csv(original, test_cat_reg);

    // Write to temp file, parse back
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_rt_eq_csv";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_rt_eq.csv").string();
    {
        std::ofstream f(file);
        f << csv_str;
    }

    auto parsed = EquipmentParser::parse_native_csv(file, test_cat_reg);

    expect(parsed.size() == original.size(), "eq CSV round-trip: same count");
    if (parsed.size() >= 1) {
        expect(parsed[0].name_id == original[0].name_id, "eq CSV round-trip: id preserved");
        expect(parsed[0].max_durability == 1561, "eq CSV round-trip: durability");
    }

    std::filesystem::remove_all(temp_dir);
}

} // namespace

int main() {
    try {
        registries::categories().initialize();
        test_json_basic();
        test_csv_basic();
        test_csv_no_durability();
        test_custom_category();
        test_missing_fields_skipped();
        test_empty_equipments();
        test_missing_equipments_key();
        test_json_mixed_with_enchantments();
        test_csv_missing_required_columns();
        test_csv_empty_file();
        test_parse_auto_detect_json();
        test_parse_auto_detect_csv();
        test_parse_mc_official_stub();
        test_parse_mc_official_with_items();
        test_parse_mc_official_no_items_dir();
        test_invalid_json_handling();
        test_non_existent_file();
        test_parse_with_invalid_format();
        test_to_json_round_trip();
        test_to_csv_round_trip();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}





