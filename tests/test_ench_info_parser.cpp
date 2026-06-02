#include "test_utils.h"
#include "parser/EnchInfoParser.h"
#include "parser/TagResolver.h"
#include "io/json.h"
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
    std::string file = "test_ench_basic.json";
    create_json(file, R"({
        "name": "Test Pack",
        "enchantments": [
            {
                "id": "sharpness",
                "name": "Sharpness",
                "platform": "java",
                "max_level": 5,
                "limited_level": 5,
                "multiplier": 1,
                "exclusive_set": ["smite"],
                "applicable_equipment": ["sword"]
            },
            {
                "id": "knockback",
                "name": "Knockback",
                "platform": "java",
                "max_level": 2,
                "limited_level": 2,
                "multiplier": 1,
                "exclusive_set": [],
                "applicable_equipment": ["sword"]
            }
        ]
    })");

    TagResolver resolver;
    auto infos = EnchInfoParser::parse_native_json(file, resolver);

    expect(infos.size() == 2, "should parse 2 enchantments");
    expect(infos[0].name_id == "sharpness", "first ench id");
    expect(infos[0].max_level == 5, "max level");
    expect(infos[0].multiplier == 1, "multiplier");
    expect(infos[0].exclusive_set.size() == 1, "exclusive set size");
    expect(infos[0].exclusive_set.contains("smite"), "exclusive contains smite");

    expect(infos[1].name_id == "knockback", "second ench id");
    expect(infos[1].max_level == 2, "knockback max level");
    expect(infos[1].exclusive_set.empty(), "knockback has no exclusives");

    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_platform_mapping
// ---------------------------------------------------------------------------
void test_platform_mapping() {
    std::string file = "test_platforms.json";
    create_json(file, R"({
        "enchantments": [
            { "id": "a", "max_level": 1, "multiplier": 1, "platform": "java" },
            { "id": "b", "max_level": 1, "multiplier": 1, "platform": "je" },
            { "id": "c", "max_level": 1, "multiplier": 1, "platform": "bedrock" },
            { "id": "d", "max_level": 1, "multiplier": 1, "platform": "be" },
            { "id": "e", "max_level": 1, "multiplier": 1, "platform": "all" },
            { "id": "f", "max_level": 1, "multiplier": 1, "platform": "both" },
            { "id": "g", "max_level": 1, "multiplier": 1, "platform": "JAVA" },
            { "id": "h", "max_level": 1, "multiplier": 1, "platform": "BedRock" },
            { "id": "i", "max_level": 1, "multiplier": 1, "platform": "invalid" },
            { "id": "j", "max_level": 1, "multiplier": 1 }
        ]
    })");

    TagResolver resolver;
    auto infos = EnchInfoParser::parse_native_json(file, resolver);

    expect(infos.size() == 10, "should parse all 10 platform entries");

    // "java" → Java
    expect(infos[0].supported_platform == platform::MCE::Java, "java -> Java");
    // "je" → Java
    expect(infos[1].supported_platform == platform::MCE::Java, "je -> Java");
    // "bedrock" → Bedrock
    expect(infos[2].supported_platform == platform::MCE::Bedrock, "bedrock -> Bedrock");
    // "be" → Bedrock
    expect(infos[3].supported_platform == platform::MCE::Bedrock, "be -> Bedrock");
    // "all" → All
    expect(infos[4].supported_platform == platform::MCE::All, "all -> All");
    // "both" → All
    expect(infos[5].supported_platform == platform::MCE::All, "both -> All");
    // "JAVA" → Java (case-insensitive)
    expect(infos[6].supported_platform == platform::MCE::Java, "JAVA -> Java");
    // "BedRock" → Bedrock (case-insensitive)
    expect(infos[7].supported_platform == platform::MCE::Bedrock, "BedRock -> Bedrock");
    // "invalid" → Java (default)
    expect(infos[8].supported_platform == platform::MCE::Java, "invalid -> Java (default)");
    // Missing → Java (default)
    expect(infos[9].supported_platform == platform::MCE::Java, "missing -> Java (default)");

    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_tag_resolution_in_exclusive_set
// ---------------------------------------------------------------------------
void test_tag_resolution_in_exclusive_set() {
    // Create a tag file on disk that the resolver can load
    std::string tag_dir = "test_ench_tag_resolve";
    create_json(
        tag_dir + "/data/minecraft/tags/enchantment/exclusive_set/undead.json",
        R"({"values": ["smite", "bane_of_arthropods"]})"
    );

    TagResolver resolver;
    resolver.load_from(tag_dir);

    // JSON that references the tag via #minecraft:exclusive_set/undead
    std::string file = "test_ench_tags.json";
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

    auto infos = EnchInfoParser::parse_native_json(file, resolver);

    expect(infos.size() == 1, "should parse 1 enchantment");
    expect(infos[0].exclusive_set.size() == 2, "should resolve tag to 2 exclusives");
    expect(infos[0].exclusive_set.contains("smite"), "resolved smite");
    expect(infos[0].exclusive_set.contains("bane_of_arthropods"), "resolved bane_of_arthropods");

    std::filesystem::remove_all(tag_dir);
    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_inline_tag_resolution
// ---------------------------------------------------------------------------
void test_inline_tag_resolution() {
    // JSON with inline tags and a reference to them
    std::string file = "test_inline_tags.json";
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

    TagResolver resolver;
    auto infos = EnchInfoParser::parse_native_json(file, resolver);

    expect(infos.size() == 1, "should parse 1 enchantment with inline tags");
    expect(infos[0].exclusive_set.size() == 3, "should resolve inline tag to 3 exclusives");
    expect(infos[0].exclusive_set.contains("sharpness"), "inline resolved sharpness");
    expect(infos[0].exclusive_set.contains("smite"), "inline resolved smite");
    expect(infos[0].exclusive_set.contains("bane_of_arthropods"), "inline resolved bane_of_arthropods");

    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_missing_fields_skipped
// ---------------------------------------------------------------------------
void test_missing_fields_skipped() {
    std::string file = "test_missing_fields.json";
    create_json(file, R"({
        "enchantments": [
            { "id": "valid",          "max_level": 3, "multiplier": 2 },
            { "max_level": 1,         "multiplier": 1 },
            { "id": "no_max_level",   "multiplier": 1 },
            { "id": "no_multiplier",  "max_level": 1 },
            { "id": "",               "max_level": 1, "multiplier": 1 }
        ]
    })");

    TagResolver resolver;
    auto infos = EnchInfoParser::parse_native_json(file, resolver);

    expect(infos.size() == 1, "only 1 valid enchantment should be parsed");
    expect(infos[0].name_id == "valid", "the valid one is 'valid'");
    expect(infos[0].max_level == 3, "max_level = 3");
    expect(infos[0].multiplier == 2, "multiplier = 2");

    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_limited_level_default
// ---------------------------------------------------------------------------
void test_limited_level_default() {
    std::string file = "test_limited.json";
    create_json(file, R"({
        "enchantments": [
            { "id": "explicit", "max_level": 5, "limited_level": 3, "multiplier": 1 },
            { "id": "defaulted", "max_level": 5, "multiplier": 1 }
        ]
    })");

    TagResolver resolver;
    auto infos = EnchInfoParser::parse_native_json(file, resolver);

    expect(infos.size() == 2, "should parse 2 entries");
    expect(infos[0].limited_level == 3, "explicit limited_level = 3");
    expect(infos[1].limited_level == 5, "defaulted limited_level = max_level = 5");

    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_name_fallback
// ---------------------------------------------------------------------------
void test_name_fallback() {
    std::string file = "test_name_fallback.json";
    create_json(file, R"({
        "enchantments": [
            { "id": "my_ench", "max_level": 1, "multiplier": 1 }
        ]
    })");

    TagResolver resolver;
    auto infos = EnchInfoParser::parse_native_json(file, resolver);

    expect(infos.size() == 1, "should parse 1 entry");
    expect(infos[0].name == "my_ench", "name should fallback to id");

    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_metadata_parsing
// ---------------------------------------------------------------------------
void test_metadata_parsing() {
    std::string file = "test_metadata.json";
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
    TagResolver resolver;
    auto infos = EnchInfoParser::parse_native_json(file, resolver, &metadata);

    expect(infos.size() == 1, "should parse 1 enchantment");
    expect(metadata.name == "My Custom Pack", "pack name");
    expect(metadata.description == "Adds fantasy enchantments", "pack description");
    expect(metadata.author == "AuthorName", "pack author");
    expect(metadata.version == "1.0.0", "pack version");

    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_null_metadata_does_not_crash
// ---------------------------------------------------------------------------
void test_null_metadata_does_not_crash() {
    std::string file = "test_null_meta.json";
    create_json(file, R"({
        "name": "Pack",
        "enchantments": [
            { "id": "a", "max_level": 1, "multiplier": 1 }
        ]
    })");

    TagResolver resolver;
    auto infos = EnchInfoParser::parse_native_json(file, resolver, nullptr);
    expect(infos.size() == 1, "null metadata should not crash");

    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_empty_enchantments_array
// ---------------------------------------------------------------------------
void test_empty_enchantments_array() {
    std::string file = "test_empty_arr.json";
    create_json(file, R"({
        "enchantments": []
    })");

    TagResolver resolver;
    auto infos = EnchInfoParser::parse_native_json(file, resolver);

    expect(infos.empty(), "empty array should return empty vector");
    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_missing_enchantments_key
// ---------------------------------------------------------------------------
void test_missing_enchantments_key() {
    std::string file = "test_no_ench_key.json";
    create_json(file, R"({
        "name": "No Enchantments Here"
    })");

    TagResolver resolver;
    auto infos = EnchInfoParser::parse_native_json(file, resolver);

    expect(infos.empty(), "missing enchantments key should return empty vector");
    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_applicable_equipment_parsing
// ---------------------------------------------------------------------------
void test_applicable_equipment_parsing() {
    std::string file = "test_equip.json";
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

    TagResolver resolver;
    auto infos = EnchInfoParser::parse_native_json(file, resolver);

    expect(infos.size() == 2, "should parse 2 entries");
    expect(infos[0].applicable_equipment.size() == 2, "sharpness has 2 equipment types");
    expect(infos[0].applicable_equipment.contains(EquipmentCategory("sword")), "contains sword");
    expect(infos[0].applicable_equipment.contains(EquipmentCategory("axe")), "contains axe");
    expect(infos[1].applicable_equipment.size() == 1, "custom equipment parsed");
    expect(infos[1].applicable_equipment.contains(EquipmentCategory("custom_weapon")), "contains custom_weapon");

    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_equipment_tag_resolution
// ---------------------------------------------------------------------------
void test_equipment_tag_resolution() {
    std::string tag_dir = "test_ench_equip_tags";
    create_json(
        tag_dir + "/data/minecraft/tags/item/weapons.json",
        R"({"values": ["sword", "axe"]})"
    );

    TagResolver resolver;
    resolver.load_from(tag_dir);

    std::string file = "test_equip_tags.json";
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

    auto infos = EnchInfoParser::parse_native_json(file, resolver);

    expect(infos.size() == 1, "should parse 1 entry");
    expect(infos[0].applicable_equipment.size() == 2, "should resolve equipment tag to 2");
    expect(infos[0].applicable_equipment.contains(EquipmentCategory("sword")), "resolved sword");
    expect(infos[0].applicable_equipment.contains(EquipmentCategory("axe")), "resolved axe");

    std::filesystem::remove_all(tag_dir);
    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_parse_method_auto_detect_json
// ---------------------------------------------------------------------------
void test_parse_method_auto_detect_json() {
    std::string file = "test_auto_detect.json";
    create_json(file, R"({
        "enchantments": [
            { "id": "a", "max_level": 1, "multiplier": 1 },
            { "id": "b", "max_level": 2, "multiplier": 2 }
        ]
    })");

    TagResolver resolver;
    auto infos = EnchInfoParser::parse(file, resolver);

    expect(infos.size() == 2, "parse() should auto-detect JSON and return 2 entries");
    expect(infos[0].name_id == "a", "first ench via parse()");

    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_cyclic_inline_tag
// ---------------------------------------------------------------------------
void test_cyclic_inline_tag() {
    std::string file = "test_cyclic_inline.json";
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

    TagResolver resolver;
    auto infos = EnchInfoParser::parse_native_json(file, resolver);

    // Cycles should be handled gracefully (no crash), may return empty set
    expect(infos.size() == 1, "cyclic inline tags should not crash");
    // The result may be empty due to cycle detection
    expect(infos[0].exclusive_set.empty(), "cyclic inline tag should resolve to empty");

    std::filesystem::remove(file);
}

} // namespace

int main() {
    try {
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
        std::cout << "PASS" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FATAL: " << e.what() << std::endl;
        return 2;
    }
}
