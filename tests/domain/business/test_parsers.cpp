#include "framework/test_utils.h"

#include "domain/business/parsers/NativeJsonParser.h"
#include "domain/business/parsers/NativeCsvParser.h"
#include "domain/business/parsers/McOfficialParser.h"
#include "domain/business/types/dto/EnchantmentData.h"
#include "domain/business/types/dto/EquipmentData.h"

#include <string>
#include <unordered_map>
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
// Include supported_items (the T2/T10 field name). The value "#minecraft:sword"
// is a tag reference passed through raw. Verify parsing completes without error.

void test_json_parse_with_applicable() {
    std::string json_str = R"({
        "enchantments": [
            {"id": "minecraft:sharpness", "name": "Sharpness", "max_level": 5, "limited_level": 5, "multiplier": 1, "supported_items": ["#minecraft:sword"]}
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

    // The supported_items field is passed through RAW (T5/T10): the
    // "#minecraft:sword" tag reference is preserved verbatim, not expanded by
    // the parser. The loader (T6) performs the cross-validation later.
    bool has_tag = false;
    for (const auto& a : enchantments[0].applicable_to) {
        if (a == "#minecraft:sword") { has_tag = true; break; }
    }
    expect(has_tag,
           "json_applicable: applicable_to contains raw #minecraft:sword");

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
//   id,name,max_level,limited_level,multiplier,exclusive_set,supported_items
// exclusive_set is semicolon-separated. Returns vector<EnchantmentData> only.
// ============================================================================

// ─── test_csv_parse_basic ──────────────────────────────────────────────
// Simple CSV with one data row. exclusive_set empty, supported_items
// quoted (contains comma-safe characters, but demonstrates quoting).

void test_csv_parse_basic() {
    std::string csv =
        "id,name,max_level,limited_level,multiplier,exclusive_set,supported_items\n"
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
        "id,name,max_level,limited_level,multiplier,exclusive_set,supported_items\n"
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
        "id,name,max_level,limited_level,multiplier,exclusive_set,supported_items\n";

    auto enchantments = NativeCsvParser::parse(csv);

    expect(enchantments.empty(), "csv_empty_header: no enchantments");

    std::cout << "PASS: test_csv_parse_empty_header_only" << std::endl;
}

// ─── test_csv_parse_multiple_rows ──────────────────────────────────────
// Three data rows. Verify all are parsed and have correct IDs.

void test_csv_parse_multiple_rows() {
    std::string csv =
        "id,name,max_level,limited_level,multiplier,exclusive_set,supported_items\n"
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
// Section C — McOfficialParser
//
// McOfficialParser parses the MC 1.21+ data-driven format.
// parse_single_enchantment() handles one enchantment JSON given a namespace,
// filename, content string, and TagResolver. parse_files() accepts a map of
// data-pack relative paths to content strings and returns paired enchantment
// and equipment vectors.
//
// JSON fields (MC 1.21+): anvil_cost (→multiplier), max_level, exclusive_set
// (→exclusive_with), supported_items (→applicable_to), min_cost (→limited_level).
// Display name is derived from the filename (e.g. "sharpness" → "Sharpness").
// ============================================================================

// ─── test_mc_single_enchantment_basic ──────────────────────────────────
// Parse a minimal enchantment JSON via parse_single_enchantment().
// supported_items "#minecraft:sword" is passed through RAW (T5) — it stays
// a tag reference and is not expanded by the parser.
// Verify id, max_level, display_name, raw applicable_to.

void test_mc_single_enchantment_basic() {
    TagResolver tag_resolver;

    std::string content = R"({
        "anvil_cost": 1,
        "max_level": 5,
        "exclusive_set": [],
        "supported_items": ["#minecraft:sword"]
    })";

    auto ench = McOfficialParser::parse_single_enchantment(
        "minecraft", "sharpness", content, tag_resolver);

    expect_eq(ench.id, std::string("minecraft:sharpness"),
              "mc_single_basic: ench id");
    expect_eq(ench.max_level, 5,
              "mc_single_basic: max_level");
    expect_eq(ench.display_name, std::string("Sharpness"),
              "mc_single_basic: display_name");
    expect(ench.multiplier > 0,
           "mc_single_basic: multiplier > 0");

    // applicable_to is passed through RAW (T5): the "#minecraft:sword" tag
    // reference survives verbatim for the loader to resolve later.
    bool has_tag = false;
    for (const auto& a : ench.applicable_to) {
        if (a == "#minecraft:sword") { has_tag = true; break; }
    }
    expect(has_tag,
           "mc_single_basic: applicable_to contains raw #minecraft:sword");

    // exclusive_set should be empty
    expect(ench.exclusive_with.empty(),
           "mc_single_basic: exclusive_with empty");

    TEST_PASS("test_mc_single_enchantment_basic");
}

// ─── test_mc_single_enchantment_with_exclusive ─────────────────────────
// Parse an enchantment with a non-empty exclusive_set containing a concrete
// enchantment ID ("minecraft:smite"). Also verify concrete supported_items
// pass through resolve() unchanged.

void test_mc_single_enchantment_with_exclusive() {
    TagResolver tag_resolver;

    std::string content = R"({
        "anvil_cost": 2,
        "max_level": 5,
        "exclusive_set": ["minecraft:smite"],
        "supported_items": ["minecraft:diamond_sword"]
    })";

    auto ench = McOfficialParser::parse_single_enchantment(
        "minecraft", "sharpness", content, tag_resolver);

    expect_eq(ench.id, std::string("minecraft:sharpness"),
              "mc_single_exclusive: ench id");
    expect_eq(ench.max_level, 5,
              "mc_single_exclusive: max_level");
    expect_eq(ench.multiplier, 2,
              "mc_single_exclusive: multiplier");

    // exclusive_set should contain "minecraft:smite"
    bool has_smite = false;
    for (const auto& e : ench.exclusive_with) {
        if (e == "minecraft:smite") { has_smite = true; break; }
    }
    expect(has_smite,
           "mc_single_exclusive: exclusive_with contains smite");

    // Concrete supported_items pass through resolve() unchanged
    bool has_sword = false;
    for (const auto& a : ench.applicable_to) {
        if (a == "minecraft:diamond_sword") { has_sword = true; break; }
    }
    expect(has_sword,
           "mc_single_exclusive: applicable_to contains diamond_sword");

    TEST_PASS("test_mc_single_enchantment_with_exclusive");
}

// ─── test_mc_limited_level_tag_resolved ────────────────────────────────
// Regression (T5 review): supported_items is passed through RAW, but the
// limited_level computation must resolve the tag to concrete items first.
// With "#minecraft:sword" → {diamond_sword (enchant 10), iron_sword
// (enchant 14)}, the max power is 44 (iron_sword):
//   (44 - 10) / 5 + 1 = 7  → capped to max_level = 5.
// If the raw "#minecraft:sword" string were fed to compute_limited_level it
// matches no item_props key and limited_level collapses to max(1, 0) = 1.

void test_mc_limited_level_tag_resolved() {
    TagResolver tag_resolver;
    tag_resolver.load_tag_content("minecraft:sword",
        R"({"values": ["minecraft:diamond_sword", "minecraft:iron_sword"]})");

    std::string content = R"({
        "anvil_cost": 1,
        "max_level": 5,
        "exclusive_set": [],
        "supported_items": ["#minecraft:sword"],
        "min_cost": {"base": 10, "per_level_above_first": 5}
    })";

    auto ench = McOfficialParser::parse_single_enchantment(
        "minecraft", "sharpness", content, tag_resolver);

    // applicable_to stays raw (pass-through, T5)
    bool has_tag = false;
    for (const auto& a : ench.applicable_to) {
        if (a == "#minecraft:sword") { has_tag = true; break; }
    }
    expect(has_tag,
           "mc_limited_tag: applicable_to raw #minecraft:sword");

    // limited_level computed from the RESOLVED items, not collapsed to 1
    expect(ench.limited_level > 1,
           "mc_limited_tag: limited_level not collapsed to 1");
    expect_eq(ench.limited_level, 5,
              "mc_limited_tag: limited_level computed from resolved items");

    TEST_PASS("test_mc_limited_level_tag_resolved");
}

// ─── test_mc_parse_files_basic ─────────────────────────────────────────
// Use parse_files() with a map containing one enchantment file and one
// item tag file. Verify the enchantment is parsed and equipment is derived
// from the tag's item IDs.

void test_mc_parse_files_basic() {
    std::unordered_map<std::string, std::string> files;

    files["data/minecraft/enchantment/sharpness.json"] = R"({
        "anvil_cost": 1,
        "max_level": 5,
        "exclusive_set": [],
        "supported_items": ["#minecraft:sword"]
    })";

    files["data/minecraft/tags/item/sword.json"] = R"({
        "values": ["minecraft:diamond_sword"]
    })";

    auto result = McOfficialParser::parse_files(files);
    const auto& enchantments = result.first;
    const auto& equipment    = result.second;

    expect_eq(static_cast<int>(enchantments.size()), 1,
              "mc_files_basic: 1 enchantment");
    expect_eq(enchantments[0].id,
              std::string("minecraft:sharpness"),
              "mc_files_basic: ench id");
    expect_eq(enchantments[0].max_level, 5,
              "mc_files_basic: max_level");

    expect_eq(static_cast<int>(equipment.size()), 1,
              "mc_files_basic: 1 equipment");
    expect_eq(equipment[0].id,
              std::string("minecraft:diamond_sword"),
              "mc_files_basic: eq id");

    TEST_PASS("test_mc_parse_files_basic");
}

// ─── test_mc_parse_files_empty ─────────────────────────────────────────
// parse_files() with an empty file map. Verify empty results with no crash.

void test_mc_parse_files_empty() {
    std::unordered_map<std::string, std::string> files;

    auto result = McOfficialParser::parse_files(files);
    const auto& enchantments = result.first;
    const auto& equipment    = result.second;

    expect(enchantments.empty(), "mc_files_empty: no enchantments");
    expect(equipment.empty(),    "mc_files_empty: no equipment");

    TEST_PASS("test_mc_parse_files_empty");
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

        // Section C — McOfficialParser
        test_mc_single_enchantment_basic();
        test_mc_single_enchantment_with_exclusive();
        test_mc_limited_level_tag_resolved();
        test_mc_parse_files_basic();
        test_mc_parse_files_empty();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
