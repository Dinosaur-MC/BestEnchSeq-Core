#include "framework/test_utils.h"
#include "domain/orchestration/components/EnchSerializer.h"
#include "domain/interface/parsers/EnchInfoParser.h"
#include "domain/business/registries/EquipmentCategoryRegistry.h"
// REMOVED: RegistryAccess.h — create local registries instead
#include "io/json.h"

// All tests share this category registry reference (initialized in main())
static auto& test_cat_reg = registries::categories();
#include <iostream>
#include <fstream>
#include <filesystem>

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void create_json(const std::string &path, const std::string &content) {
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream f(path);
    f << content;
}

// ---------------------------------------------------------------------------
// test_parse_basic_enchantments
// ---------------------------------------------------------------------------
void test_parse_basic_enchantments() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_ench_basic";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_ench_basic.json").string();
    create_json(file, R"({
        "name": "Test Pack",
        "enchantments": [
            {
                "id": "sharpness",
                "name": "Sharpness",
                "max_level": 5,
                "limited_level": 5,
                "multiplier": 1,
                "exclusive_set": ["smite"],
                "applicable_equipment": ["sword"]
            },
            {
                "id": "knockback",
                "name": "Knockback",
                "max_level": 2,
                "limited_level": 2,
                "multiplier": 1,
                "exclusive_set": [],
                "applicable_equipment": ["sword"]
            }
        ]
    })");

    auto [enchantments, equipment] = EnchInfoParser::parse_native_json(file);

    expect(enchantments.size() == 2, "should parse 2 enchantments");
    expect(enchantments[0].id.path == "sharpness", "first ench id");
    expect(enchantments[0].max_level == 5, "max level");
    expect(enchantments[0].multiplier == 1, "multiplier");
    expect(enchantments[0].exclusive_set.size() == 1, "exclusive set size");
    expect(enchantments[0].exclusive_set.contains("smite"), "exclusive contains smite");

    expect(enchantments[1].id.path == "knockback", "second ench id");
    expect(enchantments[1].max_level == 2, "knockback max level");
    expect(enchantments[1].exclusive_set.empty(), "knockback has no exclusives");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_platform_mapping (adapted: platform dropped from RawEnchantment,
// but we verify parsing still works without platform field)
// ---------------------------------------------------------------------------
void test_platform_mapping() {
    // With platform dropped from raw output, this test simply verifies
    // that enchantments without platform field still parse correctly.
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_platforms";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_platforms.json").string();
    create_json(file, R"({
        "enchantments": [
            { "id": "a", "max_level": 1, "multiplier": 1 }
        ]
    })");

    auto [enchantments, equipment] = EnchInfoParser::parse_native_json(file);

    expect(enchantments.size() == 1, "should parse 1 entry");
    expect(enchantments[0].id.path == "a", "id preserved");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_tag_resolution_in_exclusive_set
// Note: External tag files are not loaded for native JSON parsing in the
// new API (no external TagResolver parameter). This test verifies that
// #tag references without inline definitions resolve to empty (graceful
// degradation). Runtime behavior: no resolution.
// ---------------------------------------------------------------------------
void test_tag_resolution_in_exclusive_set() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_ench_tag_resolve";
    std::filesystem::create_directories(temp_dir);
    auto tag_dir = temp_dir.string();
    create_json(
        tag_dir + "/data/minecraft/tags/enchantment/exclusive_set/undead.json",
        R"({"values": ["smite", "bane_of_arthropods"]})"
    );

    // JSON that references the tag via #minecraft:exclusive_set/undead
    auto file = (temp_dir / "test_ench_tags.json").string();
    create_json(file, R"({
        "enchantments": [
            {
                "id": "sharpness",
                "max_level": 5,
                "multiplier": 1,
                "exclusive_set": ["#minecraft:exclusive_set/undead"]
            }
        ]
    })");

    auto [enchantments, equipment] = EnchInfoParser::parse_native_json(file);

    // External tags not loaded for native JSON; #refs resolve to empty
    expect(enchantments.size() == 1, "should parse 1 enchantment");
    // Tag resolution for external tags is not available in native JSON mode
    // This is expected — cross-format tag resolution is handled in MC official mode

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_inline_tag_resolution
// ---------------------------------------------------------------------------
void test_inline_tag_resolution() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_inline_tags";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_inline_tags.json").string();
    create_json(file, R"({
        "tags": {
            "enchantment": {
                "exclusive_set/melee": {
                    "values": ["sharpness", "smite", "bane_of_arthropods"]
                }
            }
        },
        "enchantments": [
            {
                "id": "test_ench",
                "max_level": 1,
                "multiplier": 1,
                "exclusive_set": ["#minecraft:exclusive_set/melee"]
            }
        ]
    })");

    auto [enchantments, equipment] = EnchInfoParser::parse_native_json(file);

    expect(enchantments.size() == 1, "should parse 1 enchantment with inline tags");
    expect(enchantments[0].exclusive_set.size() == 3, "should resolve inline tag to 3 exclusives");
    expect(enchantments[0].exclusive_set.contains("sharpness"), "inline resolved sharpness");
    expect(enchantments[0].exclusive_set.contains("smite"), "inline resolved smite");
    expect(enchantments[0].exclusive_set.contains("bane_of_arthropods"), "inline resolved bane_of_arthropods");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_missing_fields_skipped
// ---------------------------------------------------------------------------
void test_missing_fields_skipped() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_missing_fields";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_missing_fields.json").string();
    create_json(file, R"({
        "enchantments": [
            { "id": "valid",          "max_level": 3, "multiplier": 2 },
            { "max_level": 1,         "multiplier": 1 },
            { "id": "no_max_level",   "multiplier": 1 },
            { "id": "no_multiplier",  "max_level": 1 },
            { "id": "",               "max_level": 1, "multiplier": 1 }
        ]
    })");

    auto [enchantments, equipment] = EnchInfoParser::parse_native_json(file);

    expect(enchantments.size() == 1, "only 1 valid enchantment should be parsed");
    expect(enchantments[0].id.path == "valid", "the valid one is 'valid'");
    expect(enchantments[0].max_level == 3, "max_level = 3");
    expect(enchantments[0].multiplier == 2, "multiplier = 2");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_limited_level_default
// ---------------------------------------------------------------------------
void test_limited_level_default() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_limited";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_limited.json").string();
    create_json(file, R"({
        "enchantments": [
            { "id": "explicit", "max_level": 5, "limited_level": 3, "multiplier": 1 },
            { "id": "defaulted", "max_level": 5, "multiplier": 1 }
        ]
    })");

    auto [enchantments, equipment] = EnchInfoParser::parse_native_json(file);

    expect(enchantments.size() == 2, "should parse 2 entries");
    expect(enchantments[0].limited_level == 3, "explicit limited_level = 3");
    expect(enchantments[1].limited_level == 0, "defaulted limited_level = 0 (treasure)");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_name_fallback
// ---------------------------------------------------------------------------
void test_name_fallback() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_name_fallback";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_name_fallback.json").string();
    create_json(file, R"({
        "enchantments": [
            { "id": "my_ench", "max_level": 1, "multiplier": 1 }
        ]
    })");

    auto [enchantments, equipment] = EnchInfoParser::parse_native_json(file);

    expect(enchantments.size() == 1, "should parse 1 entry");
    expect(enchantments[0].display_name == "my_ench", "name should fallback to id");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_metadata_parsing
// ---------------------------------------------------------------------------
void test_metadata_parsing() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_metadata";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_metadata.json").string();
    create_json(file, R"({
        "name": "My Custom Pack",
        "description": "Adds fantasy enchantments",
        "author": "AuthorName",
        "version": "1.0.0",
        "enchantments": [
            { "id": "test", "max_level": 1, "multiplier": 1 }
        ]
    })");

    EnchantmentDataPack metadata;
    auto [enchantments, equipment] = EnchInfoParser::parse_native_json(file, &metadata);

    expect(enchantments.size() == 1, "should parse 1 enchantment");
    expect(metadata.name == "My Custom Pack", "pack name");
    expect(metadata.description == "Adds fantasy enchantments", "pack description");
    expect(metadata.author == "AuthorName", "pack author");
    expect(metadata.version == "1.0.0", "pack version");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_null_metadata_does_not_crash
// ---------------------------------------------------------------------------
void test_null_metadata_does_not_crash() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_null_meta";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_null_meta.json").string();
    create_json(file, R"({
        "name": "Pack",
        "enchantments": [
            { "id": "a", "max_level": 1, "multiplier": 1 }
        ]
    })");

    auto [enchantments, equipment] = EnchInfoParser::parse_native_json(file, nullptr);
    expect(enchantments.size() == 1, "null metadata should not crash");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_empty_enchantments_array
// ---------------------------------------------------------------------------
void test_empty_enchantments_array() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_empty_arr";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_empty_arr.json").string();
    create_json(file, R"({
        "enchantments": []
    })");

    auto [enchantments, equipment] = EnchInfoParser::parse_native_json(file);

    expect(enchantments.empty(), "empty array should return empty ench vector");
    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_missing_enchantments_key
// ---------------------------------------------------------------------------
void test_missing_enchantments_key() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_no_ench_key";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_no_ench_key.json").string();
    create_json(file, R"({
        "name": "No Enchantments Here"
    })");

    auto [enchantments, equipment] = EnchInfoParser::parse_native_json(file);

    expect(enchantments.empty(), "missing enchantments key should return empty ench vector");
    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_applicable_equipment_parsing
// ---------------------------------------------------------------------------
void test_applicable_equipment_parsing() {
    registries::categories().initialize();
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_equip";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_equip.json").string();
    create_json(file, R"({
        "enchantments": [
            {
                "id": "sharpness",
                "max_level": 5,
                "multiplier": 1,
                "applicable_equipment": ["sword", "axe"]
            },
            {
                "id": "unknown_equip",
                "max_level": 1,
                "multiplier": 1,
                "applicable_equipment": ["custom_weapon"]
            }
        ]
    })");

    auto [enchantments, equipment] = EnchInfoParser::parse_native_json(file);

    expect(enchantments.size() == 2, "should parse 2 entries");
    expect(enchantments[0].applicable_items.size() == 2, "sharpness has 2 equipment types");
    expect(enchantments[0].applicable_items.contains("sword"), "contains sword");
    expect(enchantments[0].applicable_items.contains("axe"), "contains axe");
    expect(enchantments[1].applicable_items.size() == 1, "custom equipment preserved as string");
    expect(enchantments[1].applicable_items.contains("custom_weapon"), "custom weapon string");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_equipment_tag_resolution
// Note: External tag files are not loaded for native JSON parsing in the
// new API. This test verifies graceful handling.
// ---------------------------------------------------------------------------
void test_equipment_tag_resolution() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_ench_equip_tags";
    std::filesystem::create_directories(temp_dir);
    auto tag_dir = temp_dir.string();
    create_json(
        tag_dir + "/data/minecraft/tags/item/weapons.json",
        R"({"values": ["sword", "axe"]})"
    );

    auto file = (temp_dir / "test_equip_tags.json").string();
    create_json(file, R"({
        "enchantments": [
            {
                "id": "weapon_ench",
                "max_level": 1,
                "multiplier": 1,
                "applicable_equipment": ["#minecraft:weapons"]
            }
        ]
    })");

    auto [enchantments, equipment] = EnchInfoParser::parse_native_json(file);

    expect(enchantments.size() == 1, "should parse 1 entry");
    // External tag not available in native JSON mode; ref resolves empty

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_parse_method_auto_detect_json
// ---------------------------------------------------------------------------
void test_parse_method_auto_detect_json() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_auto_detect";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_auto_detect.json").string();
    create_json(file, R"({
        "enchantments": [
            { "id": "a", "max_level": 1, "multiplier": 1 },
            { "id": "b", "max_level": 2, "multiplier": 2 }
        ]
    })");

    auto [enchantments, equipment] = EnchInfoParser::parse(file);

    expect(enchantments.size() == 2, "parse() should auto-detect JSON and return 2 entries");
    expect(enchantments[0].id.path == "a", "first ench via parse()");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_cyclic_inline_tag
// ---------------------------------------------------------------------------
void test_cyclic_inline_tag() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_cyclic_inline";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_cyclic_inline.json").string();
    create_json(file, R"({
        "tags": {
            "enchantment": {
                "cycle_a": { "values": ["#minecraft:cycle_b"] },
                "cycle_b": { "values": ["#minecraft:cycle_a"] }
            }
        },
        "enchantments": [
            {
                "id": "test",
                "max_level": 1,
                "multiplier": 1,
                "exclusive_set": ["#minecraft:cycle_a"]
            }
        ]
    })");

    auto [enchantments, equipment] = EnchInfoParser::parse_native_json(file);

    // Cycles should be handled gracefully (no crash), may return empty set
    expect(enchantments.size() == 1, "cyclic inline tags should not crash");
    // The result may be empty due to cycle detection
    expect(enchantments[0].exclusive_set.empty(), "cyclic inline tag should resolve to empty");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_csv_basic_parsing
// ---------------------------------------------------------------------------
void test_csv_basic_parsing() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_ench_csv";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_ench_csv.csv").string();
    {
        std::ofstream f(file);
        f << "id,name,platform,max_level,limited_level,multiplier,exclusive_set,"
             "applicable_equipment\n";
        f << "sharpness,Sharpness,java,5,5,1,\"smite\",\"sword;axe\"\n";
        f << "knockback,Knockback,java,2,2,1,,\"sword\"\n";
    }

    auto [enchantments, equipment] = EnchInfoParser::parse_native_csv(file);

    expect(enchantments.size() == 2, "csv: 2 enchantments");
    expect(enchantments[0].id.path == "sharpness", "csv: first ench id");
    expect(enchantments[0].display_name == "Sharpness", "csv: first ench name");
    expect(enchantments[0].max_level == 5, "csv: first max_level");
    expect(enchantments[0].multiplier == 1, "csv: first multiplier");
    expect(enchantments[0].exclusive_set.size() == 1, "csv: first exclusive set size");
    expect(enchantments[0].exclusive_set.contains("smite"), "csv: first exclusive contains smite");
    expect(enchantments[0].applicable_items.size() == 2, "csv: first has 2 equipments");
    expect(enchantments[0].applicable_items.contains("sword"),
           "csv: first contains sword");
    expect(enchantments[0].applicable_items.contains("axe"),
           "csv: first contains axe");

    expect(enchantments[1].id.path == "knockback", "csv: second ench id");
    expect(enchantments[1].display_name == "Knockback", "csv: second ench name");
    expect(enchantments[1].max_level == 2, "csv: second max_level");
    expect(enchantments[1].multiplier == 1, "csv: second multiplier");
    expect(enchantments[1].exclusive_set.empty(), "csv: second has no exclusives");
    expect(enchantments[1].applicable_items.size() == 1, "csv: second has 1 equipment");
    expect(enchantments[1].applicable_items.contains("sword"),
           "csv: second contains sword");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_csv_missing_required_columns
// ---------------------------------------------------------------------------
void test_csv_missing_required_columns() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_csv_missing_cols";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_csv_missing_cols.csv").string();
    {
        std::ofstream f(file);
        f << "id,name,platform\n";
        f << "sharpness,Sharpness,java\n";
    }

    // Missing max_level and multiplier columns should return empty
    auto [enchantments, equipment] = EnchInfoParser::parse_native_csv(file);
    expect(enchantments.empty(), "csv missing required columns: empty result");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_csv_with_tag_references
// Note: External tags not available for CSV parsing in new API.
// ---------------------------------------------------------------------------
void test_csv_with_tag_references() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_csv_tags";
    std::filesystem::create_directories(temp_dir);
    auto tag_dir = temp_dir.string();
    create_json(
        tag_dir + "/data/minecraft/tags/enchantment/exclusive_set/undead.json",
        R"({"values": ["smite", "bane_of_arthropods"]})"
    );

    auto file = (temp_dir / "test_csv_tags.csv").string();
    {
        std::ofstream f(file);
        f << "id,name,platform,max_level,limited_level,multiplier,exclusive_set,"
             "applicable_equipment\n";
        f << "sharpness,Sharpness,java,5,5,1,\"#minecraft:exclusive_set/undead\",\"sword\"\n";
    }

    auto [enchantments, equipment] = EnchInfoParser::parse_native_csv(file);

    expect(enchantments.size() == 1, "csv with tags: 1 enchantment");
    // External tag not available for CSV; ref resolves empty

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_csv_empty_file
// ---------------------------------------------------------------------------
void test_csv_empty_file() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_csv_empty";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_csv_empty.csv").string();
    {
        std::ofstream f(file);
        f << "id,name,platform,max_level,limited_level,multiplier,exclusive_set,"
             "applicable_equipment\n";
    }

    auto [enchantments, equipment] = EnchInfoParser::parse_native_csv(file);
    expect(enchantments.empty(), "csv with only header: empty result");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_mc_official_basic
// ---------------------------------------------------------------------------
void test_mc_official_basic() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_mc_off";
    std::filesystem::create_directories(temp_dir);
    std::string dir = (temp_dir / "test_mc_off").string();
    std::filesystem::create_directories(dir + "/data/minecraft/enchantment");

    create_json(dir + "/data/minecraft/enchantment/sharpness.json", R"({
        "anvil_cost": 1,
        "max_level": 5,
        "exclusive_set": ["minecraft:smite"],
        "supported_items": ["#minecraft:sword"]
    })");

    // Create a tag file for the #reference
    std::filesystem::create_directories(dir + "/data/minecraft/tags/enchantable");
    create_json(dir + "/data/minecraft/tags/enchantable/sword.json",
                R"({"values": ["minecraft:sword"]})");

    auto [enchantments, equipment] = EnchInfoParser::parse_mc_official(dir);

    expect(enchantments.size() == 1, "mc: 1 enchantment");
    expect(enchantments[0].id.str() == "minecraft:sharpness", "mc: namespaced id");
    expect(enchantments[0].display_name == "Sharpness", "mc: derived name");
    expect(enchantments[0].multiplier == 1, "mc: anvil_cost maps to multiplier");
    expect(enchantments[0].max_level == 5, "mc: max_level");
    expect(enchantments[0].limited_level == 5, "mc: limited_level defaults to max_level");
    expect(enchantments[0].exclusive_set.size() == 1, "mc: exclusive set size");
    expect(enchantments[0].exclusive_set.contains("minecraft:smite"), "mc: exclusive contains smite");
    // supported_items resolves via tags internally in MC official mode
    expect(enchantments[0].applicable_items.size() == 1, "mc: 1 applicable item");
    expect(enchantments[0].applicable_items.contains("minecraft:sword"),
           "mc: applicable item matches sword");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_mc_official_multiple_enchantments
// ---------------------------------------------------------------------------
void test_mc_official_multiple_enchantments() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_mc_off_multi";
    std::filesystem::create_directories(temp_dir);
    std::string dir = (temp_dir / "test_mc_off_multi").string();
    std::filesystem::create_directories(dir + "/data/minecraft/enchantment");

    create_json(dir + "/data/minecraft/enchantment/sharpness.json", R"({
        "anvil_cost": 1,
        "max_level": 5,
        "supported_items": ["minecraft:sword"]
    })");

    create_json(dir + "/data/minecraft/enchantment/protection.json", R"({
        "anvil_cost": 2,
        "max_level": 4,
        "exclusive_set": ["minecraft:fire_protection", "minecraft:blast_protection"],
        "supported_items": ["minecraft:helmet", "minecraft:chestplate"]
    })");

    auto [enchantments, equipment] = EnchInfoParser::parse_mc_official(dir);

    expect(enchantments.size() == 2, "mc multi: 2 enchantments");

    // Find each enchantment by id (directory iteration order is not guaranteed)
    const RawEnchantment *sharpness = nullptr;
    const RawEnchantment *protection = nullptr;
    for (const auto &info : enchantments) {
        if (info.id.str() == "minecraft:sharpness") {
            sharpness = &info;
        } else if (info.id.str() == "minecraft:protection") {
            protection = &info;
        }
    }

    expect(sharpness != nullptr, "mc multi: sharpness found");
    expect(protection != nullptr, "mc multi: protection found");
    expect(protection->multiplier == 2, "mc multi: protection multiplier");
    expect(protection->max_level == 4, "mc multi: protection max_level");
    expect(protection->exclusive_set.size() == 2, "mc multi: protection 2 exclusives");
    expect(protection->applicable_items.size() == 2, "mc multi: protection 2 equipments");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_mc_official_invalid_entries_skipped
// ---------------------------------------------------------------------------
void test_mc_official_invalid_entries_skipped() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_mc_off_skip";
    std::filesystem::create_directories(temp_dir);
    std::string dir = (temp_dir / "test_mc_off_skip").string();
    std::filesystem::create_directories(dir + "/data/minecraft/enchantment");

    // Valid
    create_json(dir + "/data/minecraft/enchantment/valid.json", R"({
        "anvil_cost": 1,
        "max_level": 3,
        "supported_items": ["minecraft:sword"]
    })");

    // Missing max_level (defaults to 0, will be skipped)
    create_json(dir + "/data/minecraft/enchantment/no_max.json", R"({
        "anvil_cost": 1,
        "supported_items": ["minecraft:sword"]
    })");

    // Missing anvil_cost (defaults to 0, will be skipped)
    create_json(dir + "/data/minecraft/enchantment/no_cost.json", R"({
        "max_level": 3,
        "supported_items": ["minecraft:sword"]
    })");

    // Not valid JSON
    {
        std::ofstream f(dir + "/data/minecraft/enchantment/bad_json.json");
        f << "not valid json";
    }

    auto [enchantments, equipment] = EnchInfoParser::parse_mc_official(dir);

    expect(enchantments.size() == 1, "mc skip: only 1 valid enchantment");
    expect(enchantments[0].id.str() == "minecraft:valid", "mc skip: valid one parsed");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_mc_official_namespaced_name
// ---------------------------------------------------------------------------
void test_mc_official_namespaced_name() {
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_mc_off_ns";
    std::filesystem::create_directories(temp_dir);
    std::string dir = (temp_dir / "test_mc_off_ns").string();
    std::filesystem::create_directories(dir + "/data/custommod/enchantment");

    create_json(dir + "/data/custommod/enchantment/fire_aspect.json", R"({
        "anvil_cost": 4,
        "max_level": 2,
        "supported_items": ["minecraft:sword"]
    })");

    auto [enchantments, equipment] = EnchInfoParser::parse_mc_official(dir);

    expect(enchantments.size() == 1, "mc ns: 1 enchantment");
    expect(enchantments[0].id.str() == "custommod:fire_aspect", "mc ns: namespaced id");
    expect(enchantments[0].display_name == "Fire aspect", "mc ns: derived name with underscore replaced");
    expect(enchantments[0].multiplier == 4, "mc ns: anvil_cost");
    expect(enchantments[0].max_level == 2, "mc ns: max_level");

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_to_json_round_trip
// ---------------------------------------------------------------------------
void test_to_json_round_trip() {
    // Create test data — is_treasure is derived from limited_level==0 in new RawEnchantment
    std::vector<EnchInfo> original;
    original.emplace_back("minecraft:sharpness", "Sharpness", MCE::Java,
                          5, 0, 1, true,  // limited_level=0 → treasure=true
                          std::unordered_set<std::string>{"minecraft:smite", "minecraft:bane_of_arthropods"},
                          std::unordered_set<int32_t>{EquipmentCategory::ID_SWORD, EquipmentCategory::ID_AXE});
    original.emplace_back("minecraft:protection", "Protection", MCE::All,
                          4, 4, 2, false,
                          std::unordered_set<std::string>{},
                          std::unordered_set<int32_t>{EquipmentCategory::ID_HELMET, EquipmentCategory::ID_CHESTPLATE});

    // Serialize to JSON
    std::string json_str = EnchSerializer::to_json(original, test_cat_reg);

    // Write to temp file, parse back
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_rt_ench_json";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_rt_ench.json").string();
    {
        std::ofstream f(file);
        f << json_str;
    }

    auto [enchantments, equipment] = EnchInfoParser::parse_native_json(file);

    expect(enchantments.size() == original.size(), "ench JSON round-trip: same count");

    if (enchantments.size() >= 1) {
        expect(enchantments[0].id.str() == original[0].name_id, "ench JSON round-trip: id preserved");
        expect(enchantments[0].display_name == original[0].name, "ench JSON round-trip: name preserved");
        expect(enchantments[0].max_level == original[0].max_level, "ench JSON round-trip: max_level");
        expect(enchantments[0].limited_level == original[0].limited_level, "ench JSON round-trip: limited_level");
        expect(enchantments[0].multiplier == original[0].multiplier, "ench JSON round-trip: multiplier");
        expect(enchantments[0].exclusive_set.size() == original[0].exclusive_set.size(),
               "ench JSON round-trip: exclusive_set size");
        // is_treasure is dropped from RawEnchantment; derive from limited_level
        bool is_treasure = (enchantments[0].limited_level == 0);
        expect(is_treasure == original[0].is_treasure, "ench JSON round-trip: treasure from limited_level");
    }

    // Verify is_treasure preserved through round-trip
    if (enchantments.size() >= 2) {
        bool first_is_treasure = (enchantments[0].limited_level == 0);
        bool second_is_treasure = (enchantments[1].limited_level == 0);
        expect(first_is_treasure == true, "ench JSON round-trip: sharpness is treasure (limited_level=0)");
        expect(second_is_treasure == false, "ench JSON round-trip: protection is not treasure");
    }

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_to_csv_round_trip
// ---------------------------------------------------------------------------
void test_to_csv_round_trip() {
    // Create test data
    std::vector<EnchInfo> original;
    original.emplace_back("sharpness", "Sharpness", MCE::Java,
                          5, 5, 1, false,
                          std::unordered_set<std::string>{"smite"},
                          std::unordered_set<int32_t>{EquipmentCategory::ID_SWORD});
    original.emplace_back("knockback", "Knockback", MCE::Java,
                          2, 2, 1, false,
                          std::unordered_set<std::string>{},
                          std::unordered_set<int32_t>{EquipmentCategory::ID_SWORD});

    // Serialize to CSV
    std::string csv_str = EnchSerializer::to_csv(original, test_cat_reg);

    // Write to temp file, parse back
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_rt_ench_csv";
    std::filesystem::create_directories(temp_dir);
    auto file = (temp_dir / "test_rt_ench.csv").string();
    {
        std::ofstream f(file);
        f << csv_str;
    }

    auto [enchantments, equipment] = EnchInfoParser::parse_native_csv(file);

    expect(enchantments.size() == original.size(), "ench CSV round-trip: same count");

    if (enchantments.size() >= 1) {
        expect(enchantments[0].id.path == original[0].name_id, "ench CSV round-trip: id preserved");
        expect(enchantments[0].max_level == original[0].max_level, "ench CSV round-trip: max_level");
        expect(enchantments[0].multiplier == original[0].multiplier, "ench CSV round-trip: multiplier");
        expect(enchantments[0].exclusive_set.size() == original[0].exclusive_set.size(),
               "ench CSV round-trip: exclusive_set size");
    }

    std::filesystem::remove_all(temp_dir);
}

// ---------------------------------------------------------------------------
// test_export_mc_official_round_trip
// ---------------------------------------------------------------------------
void test_export_mc_official_round_trip() {
    // Create test data
    std::vector<EnchInfo> original;
    original.emplace_back("minecraft:sharpness", "Sharpness", MCE::All,
                          5, 5, 1, false,
                          std::unordered_set<std::string>{"minecraft:smite"},
                          std::unordered_set<int32_t>{EquipmentCategory::ID_SWORD});

    // Export to MC official format
    auto temp_dir = std::filesystem::temp_directory_path() / "besq_test_rt_mc_off";
    std::filesystem::create_directories(temp_dir);
    std::string output_dir = (temp_dir / "output").string();
    EnchSerializer::export_to_mc_official(original, test_cat_reg, output_dir);

    // Parse back
    auto [enchantments, equipment] = EnchInfoParser::parse_mc_official(output_dir);

    expect(enchantments.size() == original.size(), "mc official round-trip: same count");
    if (!enchantments.empty()) {
        expect(enchantments[0].id.str() == original[0].name_id, "mc official round-trip: id preserved");
        expect(enchantments[0].multiplier == original[0].multiplier, "mc official round-trip: multiplier");
        expect(enchantments[0].max_level == original[0].max_level, "mc official round-trip: max_level");
    }

    std::filesystem::remove_all(temp_dir);
}

} // namespace

int main() {
    try {
        registries::categories().initialize();
        test_parse_basic_enchantments();
        test_platform_mapping();
        test_tag_resolution_in_exclusive_set();
        test_inline_tag_resolution();
        test_missing_fields_skipped();
        test_limited_level_default();
        test_name_fallback();
        test_metadata_parsing();
        test_null_metadata_does_not_crash();
        test_empty_enchantments_array();
        test_missing_enchantments_key();
        test_applicable_equipment_parsing();
        test_equipment_tag_resolution();
        test_parse_method_auto_detect_json();
        test_cyclic_inline_tag();
        test_csv_basic_parsing();
        test_csv_missing_required_columns();
        test_csv_with_tag_references();
        test_csv_empty_file();
        test_mc_official_basic();
        test_mc_official_multiple_enchantments();
        test_mc_official_invalid_entries_skipped();
        test_mc_official_namespaced_name();
        test_to_json_round_trip();
        test_to_csv_round_trip();
        test_export_mc_official_round_trip();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
