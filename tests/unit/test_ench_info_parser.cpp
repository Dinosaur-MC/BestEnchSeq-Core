#include "framework/test_utils.h"
#include "parser/EnchInfoParser.h"
#include "parser/TagResolver.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/RegistryAccess.h"
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
    expect(infos[0].supported_platform == MCE::Java, "java -> Java");
    // "je" → Java
    expect(infos[1].supported_platform == MCE::Java, "je -> Java");
    // "bedrock" → Bedrock
    expect(infos[2].supported_platform == MCE::Bedrock, "bedrock -> Bedrock");
    // "be" → Bedrock
    expect(infos[3].supported_platform == MCE::Bedrock, "be -> Bedrock");
    // "all" → All
    expect(infos[4].supported_platform == MCE::All, "all -> All");
    // "both" → All
    expect(infos[5].supported_platform == MCE::All, "both -> All");
    // "JAVA" → Java (case-insensitive)
    expect(infos[6].supported_platform == MCE::Java, "JAVA -> Java");
    // "BedRock" → Bedrock (case-insensitive)
    expect(infos[7].supported_platform == MCE::Bedrock, "BedRock -> Bedrock");
    // "invalid" → Java (default)
    expect(infos[8].supported_platform == MCE::Java, "invalid -> Java (default)");
    // Missing → Java (default)
    expect(infos[9].supported_platform == MCE::Java, "missing -> Java (default)");

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
    registries::categories().initialize();
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
    expect(infos[0].applicable_category_ids.size() == 2, "sharpness has 2 equipment types");
    expect(infos[0].applicable_category_ids.contains(EquipmentCategory::ID_SWORD), "contains sword");
    expect(infos[0].applicable_category_ids.contains(EquipmentCategory::ID_AXE), "contains axe");
    expect(infos[1].applicable_category_ids.empty(), "custom equipment skipped (not in registry)");

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
    expect(infos[0].applicable_category_ids.size() == 2, "should resolve equipment tag to 2");
    expect(infos[0].applicable_category_ids.contains(EquipmentCategory::ID_SWORD), "resolved sword");
    expect(infos[0].applicable_category_ids.contains(EquipmentCategory::ID_AXE), "resolved axe");

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

// ---------------------------------------------------------------------------
// test_csv_basic_parsing
// ---------------------------------------------------------------------------
void test_csv_basic_parsing() {
    std::string file = "test_ench_csv.csv";
    {
        std::ofstream f(file);
        f << "id,name,platform,max_level,limited_level,multiplier,exclusive_set,"
             "applicable_equipment\n";
        f << "sharpness,Sharpness,java,5,5,1,\"smite\",\"sword;axe\"\n";
        f << "knockback,Knockback,java,2,2,1,,\"sword\"\n";
    }

    TagResolver resolver;
    auto infos = EnchInfoParser::parse_native_csv(file, resolver);

    expect(infos.size() == 2, "csv: 2 enchantments");
    expect(infos[0].name_id == "sharpness", "csv: first ench id");
    expect(infos[0].name == "Sharpness", "csv: first ench name");
    expect(infos[0].max_level == 5, "csv: first max_level");
    expect(infos[0].multiplier == 1, "csv: first multiplier");
    expect(infos[0].exclusive_set.size() == 1, "csv: first exclusive set size");
    expect(infos[0].exclusive_set.contains("smite"), "csv: first exclusive contains smite");
    expect(infos[0].applicable_category_ids.size() == 2, "csv: first has 2 equipments");
    expect(infos[0].applicable_category_ids.contains(EquipmentCategory::ID_SWORD),
           "csv: first contains sword");
    expect(infos[0].applicable_category_ids.contains(EquipmentCategory::ID_AXE),
           "csv: first contains axe");

    expect(infos[1].name_id == "knockback", "csv: second ench id");
    expect(infos[1].name == "Knockback", "csv: second ench name");
    expect(infos[1].max_level == 2, "csv: second max_level");
    expect(infos[1].multiplier == 1, "csv: second multiplier");
    expect(infos[1].exclusive_set.empty(), "csv: second has no exclusives");
    expect(infos[1].applicable_category_ids.size() == 1, "csv: second has 1 equipment");
    expect(infos[1].applicable_category_ids.contains(EquipmentCategory::ID_SWORD),
           "csv: second contains sword");

    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_csv_missing_required_columns
// ---------------------------------------------------------------------------
void test_csv_missing_required_columns() {
    std::string file = "test_csv_missing_cols.csv";
    {
        std::ofstream f(file);
        f << "id,name,platform\n";
        f << "sharpness,Sharpness,java\n";
    }

    TagResolver resolver;
    // Missing max_level and multiplier columns should return empty
    auto infos = EnchInfoParser::parse_native_csv(file, resolver);
    expect(infos.empty(), "csv missing required columns: empty result");

    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_csv_with_tag_references
// ---------------------------------------------------------------------------
void test_csv_with_tag_references() {
    std::string tag_dir = "test_csv_tags";
    create_json(
        tag_dir + "/data/minecraft/tags/enchantment/exclusive_set/undead.json",
        R"({"values": ["smite", "bane_of_arthropods"]})"
    );

    TagResolver resolver;
    resolver.load_from(tag_dir);

    std::string file = "test_csv_tags.csv";
    {
        std::ofstream f(file);
        f << "id,name,platform,max_level,limited_level,multiplier,exclusive_set,"
             "applicable_equipment\n";
        f << "sharpness,Sharpness,java,5,5,1,\"#minecraft:exclusive_set/undead\",\"sword\"\n";
    }

    auto infos = EnchInfoParser::parse_native_csv(file, resolver);

    expect(infos.size() == 1, "csv with tags: 1 enchantment");
    expect(infos[0].exclusive_set.size() == 2, "csv with tags: resolved 2 exclusives");
    expect(infos[0].exclusive_set.contains("smite"), "csv with tags: contains smite");
    expect(infos[0].exclusive_set.contains("bane_of_arthropods"),
           "csv with tags: contains bane_of_arthropods");

    std::filesystem::remove_all(tag_dir);
    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_csv_empty_file
// ---------------------------------------------------------------------------
void test_csv_empty_file() {
    std::string file = "test_csv_empty.csv";
    {
        std::ofstream f(file);
        f << "id,name,platform,max_level,limited_level,multiplier,exclusive_set,"
             "applicable_equipment\n";
    }

    TagResolver resolver;
    auto infos = EnchInfoParser::parse_native_csv(file, resolver);
    expect(infos.empty(), "csv with only header: empty result");

    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_mc_official_basic
// ---------------------------------------------------------------------------
void test_mc_official_basic() {
    std::string dir = "test_mc_off";
    std::filesystem::create_directories(dir + "/data/minecraft/enchantment");

    create_json(dir + "/data/minecraft/enchantment/sharpness.json", R"({
        "anvil_cost": 1,
        "max_level": 5,
        "exclusive_set": ["minecraft:smite"],
        "supported_items": ["#minecraft:sword"]
    })");

    TagResolver resolver;
    // Create a tag file for the # reference at data/minecraft/tags/enchantable/sword.json
    // TagResolver stores the key as "minecraft:sword" (relative path from category dir)
    std::filesystem::create_directories(dir + "/data/minecraft/tags/enchantable");
    create_json(dir + "/data/minecraft/tags/enchantable/sword.json",
                R"({"values": ["minecraft:sword"]})");

    auto infos = EnchInfoParser::parse_mc_official(dir, resolver);

    expect(infos.size() == 1, "mc: 1 enchantment");
    expect(infos[0].name_id == "minecraft:sharpness", "mc: namespaced id");
    expect(infos[0].name == "Sharpness", "mc: derived name");
    expect(infos[0].multiplier == 1, "mc: anvil_cost maps to multiplier");
    expect(infos[0].max_level == 5, "mc: max_level");
    expect(infos[0].limited_level == 5, "mc: limited_level defaults to max_level");
    expect(infos[0].supported_platform == MCE::All, "mc: platform defaults to All");
    expect(infos[0].exclusive_set.size() == 1, "mc: exclusive set size");
    expect(infos[0].exclusive_set.contains("minecraft:smite"), "mc: exclusive contains smite");
    expect(infos[0].applicable_category_ids.size() == 1, "mc: 1 applicable equipment");
    expect(infos[0].applicable_category_ids.contains(EquipmentCategory::ID_SWORD),
           "mc: applicable equipment matches sword");

    std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------
// test_mc_official_multiple_enchantments
// ---------------------------------------------------------------------------
void test_mc_official_multiple_enchantments() {
    std::string dir = "test_mc_off_multi";
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

    TagResolver resolver;
    auto infos = EnchInfoParser::parse_mc_official(dir, resolver);

    expect(infos.size() == 2, "mc multi: 2 enchantments");

    // Find each enchantment by id (directory iteration order is not guaranteed)
    const EnchInfo *sharpness = nullptr;
    const EnchInfo *protection = nullptr;
    for (const auto &info : infos) {
        if (info.name_id == "minecraft:sharpness") {
            sharpness = &info;
        } else if (info.name_id == "minecraft:protection") {
            protection = &info;
        }
    }

    expect(sharpness != nullptr, "mc multi: sharpness found");
    expect(protection != nullptr, "mc multi: protection found");
    expect(protection->multiplier == 2, "mc multi: protection multiplier");
    expect(protection->max_level == 4, "mc multi: protection max_level");
    expect(protection->exclusive_set.size() == 2, "mc multi: protection 2 exclusives");
    expect(protection->applicable_category_ids.size() == 2, "mc multi: protection 2 equipments");

    std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------
// test_mc_official_invalid_entries_skipped
// ---------------------------------------------------------------------------
void test_mc_official_invalid_entries_skipped() {
    std::string dir = "test_mc_off_skip";
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

    TagResolver resolver;
    auto infos = EnchInfoParser::parse_mc_official(dir, resolver);

    expect(infos.size() == 1, "mc skip: only 1 valid enchantment");
    expect(infos[0].name_id == "minecraft:valid", "mc skip: valid one parsed");

    std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------
// test_mc_official_namespaced_name
// ---------------------------------------------------------------------------
void test_mc_official_namespaced_name() {
    std::string dir = "test_mc_off_ns";
    std::filesystem::create_directories(dir + "/data/custommod/enchantment");

    create_json(dir + "/data/custommod/enchantment/fire_aspect.json", R"({
        "anvil_cost": 4,
        "max_level": 2,
        "supported_items": ["minecraft:sword"]
    })");

    TagResolver resolver;
    auto infos = EnchInfoParser::parse_mc_official(dir, resolver);

    expect(infos.size() == 1, "mc ns: 1 enchantment");
    expect(infos[0].name_id == "custommod:fire_aspect", "mc ns: namespaced id");
    expect(infos[0].name == "Fire aspect", "mc ns: derived name with underscore replaced");
    expect(infos[0].multiplier == 4, "mc ns: anvil_cost");
    expect(infos[0].max_level == 2, "mc ns: max_level");

    std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------
// test_to_json_round_trip
// ---------------------------------------------------------------------------
void test_to_json_round_trip() {
    // Create test data
    std::vector<EnchInfo> original;
    original.emplace_back("minecraft:sharpness", "Sharpness", MCE::Java,
                          5, 5, 1,
                          std::unordered_set<std::string>{"minecraft:smite", "minecraft:bane_of_arthropods"},
                          std::unordered_set<int32_t>{EquipmentCategory::ID_SWORD, EquipmentCategory::ID_AXE});
    original.emplace_back("minecraft:protection", "Protection", MCE::All,
                          4, 4, 2,
                          std::unordered_set<std::string>{},
                          std::unordered_set<int32_t>{EquipmentCategory::ID_HELMET, EquipmentCategory::ID_CHESTPLATE});

    // Serialize to JSON
    std::string json_str = EnchInfoParser::to_json(original);

    // Write to temp file, parse back
    std::string file = "test_rt_ench.json";
    {
        std::ofstream f(file);
        f << json_str;
    }

    TagResolver resolver;
    auto parsed = EnchInfoParser::parse_native_json(file, resolver);

    expect(parsed.size() == original.size(), "ench JSON round-trip: same count");

    if (parsed.size() >= 1) {
        expect(parsed[0].name_id == original[0].name_id, "ench JSON round-trip: id preserved");
        expect(parsed[0].name == original[0].name, "ench JSON round-trip: name preserved");
        expect(parsed[0].supported_platform == original[0].supported_platform,
               "ench JSON round-trip: platform preserved");
        expect(parsed[0].max_level == original[0].max_level, "ench JSON round-trip: max_level");
        expect(parsed[0].limited_level == original[0].limited_level, "ench JSON round-trip: limited_level");
        expect(parsed[0].multiplier == original[0].multiplier, "ench JSON round-trip: multiplier");
        expect(parsed[0].exclusive_set.size() == original[0].exclusive_set.size(),
               "ench JSON round-trip: exclusive_set size");
    }

    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_to_csv_round_trip
// ---------------------------------------------------------------------------
void test_to_csv_round_trip() {
    // Create test data
    std::vector<EnchInfo> original;
    original.emplace_back("sharpness", "Sharpness", MCE::Java,
                          5, 5, 1,
                          std::unordered_set<std::string>{"smite"},
                          std::unordered_set<int32_t>{EquipmentCategory::ID_SWORD});
    original.emplace_back("knockback", "Knockback", MCE::Java,
                          2, 2, 1,
                          std::unordered_set<std::string>{},
                          std::unordered_set<int32_t>{EquipmentCategory::ID_SWORD});

    // Serialize to CSV
    std::string csv_str = EnchInfoParser::to_csv(original);

    // Write to temp file, parse back
    std::string file = "test_rt_ench.csv";
    {
        std::ofstream f(file);
        f << csv_str;
    }

    TagResolver resolver;
    auto parsed = EnchInfoParser::parse_native_csv(file, resolver);

    expect(parsed.size() == original.size(), "ench CSV round-trip: same count");

    if (parsed.size() >= 1) {
        expect(parsed[0].name_id == original[0].name_id, "ench CSV round-trip: id preserved");
        expect(parsed[0].max_level == original[0].max_level, "ench CSV round-trip: max_level");
        expect(parsed[0].multiplier == original[0].multiplier, "ench CSV round-trip: multiplier");
        expect(parsed[0].exclusive_set.size() == original[0].exclusive_set.size(),
               "ench CSV round-trip: exclusive_set size");
    }

    std::filesystem::remove(file);
}

// ---------------------------------------------------------------------------
// test_export_mc_official_round_trip
// ---------------------------------------------------------------------------
void test_export_mc_official_round_trip() {
    // Create test data
    std::vector<EnchInfo> original;
    original.emplace_back("minecraft:sharpness", "Sharpness", MCE::All,
                          5, 5, 1,
                          std::unordered_set<std::string>{"minecraft:smite"},
                          std::unordered_set<int32_t>{EquipmentCategory::ID_SWORD});

    // Export to MC official format
    std::string output_dir = "test_rt_mc_off";
    EnchInfoParser::export_to_mc_official(original, output_dir);

    // Parse back
    TagResolver resolver;
    auto parsed = EnchInfoParser::parse_mc_official(output_dir, resolver);

    expect(parsed.size() == original.size(), "mc official round-trip: same count");
    if (!parsed.empty()) {
        expect(parsed[0].name_id == original[0].name_id, "mc official round-trip: id preserved");
        expect(parsed[0].multiplier == original[0].multiplier, "mc official round-trip: multiplier");
        expect(parsed[0].max_level == original[0].max_level, "mc official round-trip: max_level");
    }

    std::filesystem::remove_all(output_dir);
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

