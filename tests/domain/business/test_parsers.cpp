#include "framework/test_utils.h"

#include "domain/business/parsers/NativeJsonParser.h"
#include "domain/business/parsers/NativeCsvParser.h"
#include "domain/business/types/dto/EnchantmentData.h"
#include "domain/business/types/dto/EquipmentData.h"

#include <string>
#include <vector>

// ============================================================================
// Section A — NativeJsonParser
//
// NativeJsonParser parses an all-in-one JSON file containing "enchantments",
// "equipments", and "tags" top-level keys. Returns a pair of vectors:
//   (vector<EnchantmentData>, vector<EquipmentData>)
//
// Field note: The README mentioned fields "name", "platform", "is_curse",
// "exclusive_with", "applicable_equipment" but the actual DTO structs use
// "display_name" for the display name, "exclusive_with" for conflicts, and
// "applicable_to" for applicable equipment. This test uses the actual field
// names from the source.
// ============================================================================

// ─── test_json_parse_basic ─────────────────────────────────────────────
// Parse a minimal JSON string matching the vanilla.json structure via
// parse_string(). Verify enchantments and equipment are parsed correctly.

void test_json_parse_basic() {
    std::string json_str = R"({
        "enchantments": [
            {"id": "minecraft:sharpness", "name": "Sharpness", "max_level": 5, "limited_level": 5, "multiplier": 1}
        ],
        "equipments": [
            {"id": "minecraft:diamond_sword", "name": "Diamond Sword", "category": "sword", "max_durability": 1561}
        ],
        "tags": [
            {"name": "sword", "values": ["#minecraft:swords"]}
        ]
    })";

    auto result = NativeJsonParser::parse_string(json_str);
    const auto& enchantments = result.first;
    const auto& equipment    = result.second;

    // Enchantment checks
    expect_eq(static_cast<int>(enchantments.size()), 1,
              "json_basic: 1 enchantment");
    expect_eq(enchantments[0].id,
              std::string("minecraft:sharpness"),
              "json_basic: ench id");
    expect_eq(enchantments[0].display_name,
              std::string("Sharpness"),
              "json_basic: ench display_name");
    expect_eq(enchantments[0].max_level, 5,
              "json_basic: ench max_level");

    // Equipment checks
    expect_eq(static_cast<int>(equipment.size()), 1,
              "json_basic: 1 equipment");
    expect_eq(equipment[0].id,
              std::string("minecraft:diamond_sword"),
              "json_basic: eq id");
    expect_eq(equipment[0].display_name,
              std::string("Diamond Sword"),
              "json_basic: eq display_name");
    expect_eq(equipment[0].category,
              std::string("sword"),
              "json_basic: eq category");

    std::cout << "PASS: test_json_parse_basic" << std::endl;
}

// ─── test_json_parse_with_exclusive ────────────────────────────────────
// Include exclusive_set field. Concrete IDs (no "#" prefix) pass through
// resolve_references unchanged. Verify the value is present.

void test_json_parse_with_exclusive() {
    std::string json_str = R"({
        "enchantments": [
            {"id": "minecraft:sharpness", "name": "Sharpness", "max_level": 5, "limited_level": 5, "multiplier": 1, "exclusive_set": ["minecraft:smite"]}
        ],
        "equipments": [
            {"id": "minecraft:diamond_sword", "name": "Diamond Sword", "category": "sword", "max_durability": 1561}
        ]
    })";

    auto result = NativeJsonParser::parse_string(json_str);
    const auto& enchantments = result.first;

    expect_eq(static_cast<int>(enchantments.size()), 1,
              "json_exclusive: 1 enchantment");
    expect_eq(enchantments[0].id,
              std::string("minecraft:sharpness"),
              "json_exclusive: ench id");

    // exclusive_set value "minecraft:smite" has no '#' prefix, so
    // TagResolver::resolve() returns it as-is.
    bool has_smite = false;
    for (const auto& excl : enchantments[0].exclusive_with) {
        if (excl == "minecraft:smite") { has_smite = true; break; }
    }
    expect(has_smite, "json_exclusive: exclusive_with contains smite");

    std::cout << "PASS: test_json_parse_with_exclusive" << std::endl;
}

// ─── test_json_parse_with_applicable ───────────────────────────────────
// Include applicable_equipment. The value "#minecraft:sword" is a tag
// reference that resolves to empty via TagResolver when no tags are
// loaded. Verify parsing completes without error.

void test_json_parse_with_applicable() {
    std::string json_str = R"({
        "enchantments": [
            {"id": "minecraft:sharpness", "name": "Sharpness", "max_level": 5, "limited_level": 5, "multiplier": 1, "applicable_equipment": ["#minecraft:sword"]}
        ],
        "equipments": [
            {"id": "minecraft:diamond_sword", "name": "Diamond Sword", "category": "sword", "max_durability": 1561}
        ]
    })";

    auto result = NativeJsonParser::parse_string(json_str);
    const auto& enchantments = result.first;

    expect_eq(static_cast<int>(enchantments.size()), 1,
              "json_applicable: 1 enchantment");
    expect_eq(enchantments[0].id,
              std::string("minecraft:sharpness"),
              "json_applicable: ench id");

    // The applicable_equipment field is read from JSON and passed through
    // resolve_references. Without a tag definition for "#minecraft:sword",
    // it resolves to an empty set. Verify no crash.
    expect(enchantments[0].applicable_to.empty(),
           "json_applicable: applicable_to empty (tag unresolved)");

    std::cout << "PASS: test_json_parse_with_applicable" << std::endl;
}

// ─── test_json_parse_empty ─────────────────────────────────────────────
// parse_string("{}"). Verify empty results, no crash.

void test_json_parse_empty() {
    auto result = NativeJsonParser::parse_string("{}");
    const auto& enchantments = result.first;
    const auto& equipment    = result.second;

    expect(enchantments.empty(), "json_empty: no enchantments");
    expect(equipment.empty(),    "json_empty: no equipment");

    std::cout << "PASS: test_json_parse_empty" << std::endl;
}

// ─── test_json_parse_via_Json ──────────────────────────────────────────
// Construct a Json DOM manually using the builder API (Json::object(),
// Json::array(), set(), push_back()), then call parse(json). Verify results.

void test_json_parse_via_Json() {
    // Build the JSON DOM manually
    Json root = Json::object();

    // -- Enchantments array --
    Json enchs = Json::array();
    {
        Json ench = Json::object();
        ench.set("id",             Json("minecraft:sharpness"));
        ench.set("name",           Json("Sharpness"));
        ench.set("max_level",      Json(5));
        ench.set("limited_level",  Json(5));
        ench.set("multiplier",     Json(1));
        enchs.push_back(ench);
    }
    root.set("enchantments", enchs);

    // -- Equipments array --
    Json eqs = Json::array();
    {
        Json eq = Json::object();
        eq.set("id",              Json("minecraft:diamond_sword"));
        eq.set("name",            Json("Diamond Sword"));
        eq.set("category",        Json("sword"));
        eq.set("max_durability",  Json(1561));
        eqs.push_back(eq);
    }
    root.set("equipments", eqs);

    // Parse via the Json DOM overload
    auto result = NativeJsonParser::parse(root);
    const auto& enchantments = result.first;
    const auto& equipment    = result.second;

    // Enchantment checks
    expect_eq(static_cast<int>(enchantments.size()), 1,
              "json_manual: 1 enchantment");
    expect_eq(enchantments[0].id,
              std::string("minecraft:sharpness"),
              "json_manual: ench id");
    expect_eq(enchantments[0].display_name,
              std::string("Sharpness"),
              "json_manual: ench display_name");
    expect_eq(enchantments[0].max_level, 5,
              "json_manual: ench max_level");

    // Equipment checks
    expect_eq(static_cast<int>(equipment.size()), 1,
              "json_manual: 1 equipment");
    expect_eq(equipment[0].id,
              std::string("minecraft:diamond_sword"),
              "json_manual: eq id");
    expect_eq(equipment[0].display_name,
              std::string("Diamond Sword"),
              "json_manual: eq display_name");
    expect_eq(equipment[0].category,
              std::string("sword"),
              "json_manual: eq category");

    std::cout << "PASS: test_json_parse_via_Json" << std::endl;
}

// ============================================================================
// Section B — NativeCsvParser
//
// NativeCsvParser reads CSV content with columns:
//   id,name,max_level,limited_level,multiplier,exclusive_set,applicable_equipment
// exclusive_set is semicolon-separated. Returns vector<EnchantmentData> only.
// ============================================================================

// ─── test_csv_parse_basic ──────────────────────────────────────────────
// Simple CSV with one data row. exclusive_set empty, applicable_equipment
// quoted (contains comma-safe characters, but demonstrates quoting).

void test_csv_parse_basic() {
    std::string csv =
        "id,name,max_level,limited_level,multiplier,exclusive_set,applicable_equipment\n"
        "minecraft:sharpness,Sharpness,5,5,1,,\"#minecraft:sword\"\n";

    auto enchantments = NativeCsvParser::parse(csv);

    expect_eq(static_cast<int>(enchantments.size()), 1,
              "csv_basic: 1 enchantment");
    expect_eq(enchantments[0].id,
              std::string("minecraft:sharpness"),
              "csv_basic: ench id");
    expect_eq(enchantments[0].display_name,
              std::string("Sharpness"),
              "csv_basic: ench display_name");
    expect_eq(enchantments[0].max_level, 5,
              "csv_basic: ench max_level");
    expect_eq(enchantments[0].multiplier, 1,
              "csv_basic: ench multiplier");

    std::cout << "PASS: test_csv_parse_basic" << std::endl;
}

// ─── test_csv_parse_with_exclusive ─────────────────────────────────────
// Include exclusive_set column with a concrete ID. Note the CSV format
// uses semicolons as delimiters inside the exclusive_set cell; a single
// value does not need a semicolon.

void test_csv_parse_with_exclusive() {
    std::string csv =
        "id,name,max_level,limited_level,multiplier,exclusive_set,applicable_equipment\n"
        "minecraft:sharpness,Sharpness,5,5,1,minecraft:smite,#minecraft:sword\n";

    auto enchantments = NativeCsvParser::parse(csv);

    expect_eq(static_cast<int>(enchantments.size()), 1,
              "csv_exclusive: 1 enchantment");
    expect_eq(enchantments[0].id,
              std::string("minecraft:sharpness"),
              "csv_exclusive: ench id");

    bool has_smite = false;
    for (const auto& excl : enchantments[0].exclusive_with) {
        if (excl == "minecraft:smite") { has_smite = true; break; }
    }
    expect(has_smite, "csv_exclusive: exclusive_with contains smite");

    std::cout << "PASS: test_csv_parse_with_exclusive" << std::endl;
}

// ─── test_csv_parse_empty_header_only ──────────────────────────────────
// Only the header row — no data rows. Verify empty result.

void test_csv_parse_empty_header_only() {
    std::string csv =
        "id,name,max_level,limited_level,multiplier,exclusive_set,applicable_equipment\n";

    auto enchantments = NativeCsvParser::parse(csv);

    expect(enchantments.empty(), "csv_empty_header: no enchantments");

    std::cout << "PASS: test_csv_parse_empty_header_only" << std::endl;
}

// ─── test_csv_parse_multiple_rows ──────────────────────────────────────
// Three data rows. Verify all are parsed and have correct IDs.

void test_csv_parse_multiple_rows() {
    std::string csv =
        "id,name,max_level,limited_level,multiplier,exclusive_set,applicable_equipment\n"
        "minecraft:sharpness,Sharpness,5,5,1,,\n"
        "minecraft:smite,Smite,5,5,1,,\n"
        "minecraft:unbreaking,Unbreaking,3,3,1,,\n";

    auto enchantments = NativeCsvParser::parse(csv);

    expect_eq(static_cast<int>(enchantments.size()), 3,
              "csv_multi: 3 enchantments");
    expect_eq(enchantments[0].id,
              std::string("minecraft:sharpness"),
              "csv_multi: ench[0].id");
    expect_eq(enchantments[1].id,
              std::string("minecraft:smite"),
              "csv_multi: ench[1].id");
    expect_eq(enchantments[2].id,
              std::string("minecraft:unbreaking"),
              "csv_multi: ench[2].id");

    std::cout << "PASS: test_csv_parse_multiple_rows" << std::endl;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    try {
        // Section A — NativeJsonParser
        test_json_parse_basic();
        test_json_parse_with_exclusive();
        test_json_parse_with_applicable();
        test_json_parse_empty();
        test_json_parse_via_Json();

        // Section B — NativeCsvParser
        test_csv_parse_basic();
        test_csv_parse_with_exclusive();
        test_csv_parse_empty_header_only();
        test_csv_parse_multiple_rows();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
