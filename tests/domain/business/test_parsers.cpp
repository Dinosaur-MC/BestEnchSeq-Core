#include "framework/test_utils.h"

#include "domain/business/parsers/NativeJsonParser.h"
#include "domain/business/parsers/NativeCsvParser.h"
#include "domain/business/parsers/McOfficialParser.h"
#include "domain/business/types/dto/EnchantmentData.h"
#include "domain/business/types/dto/EquipmentData.h"
#include "domain/business/components/TagResolver.h"
#include "common/io/FileUtils.hpp"

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

// ─── test_json_parse_min_cost_flat ─────────────────────────────────────
// B-T17: flat min_cost_base / min_cost_per_level fields populate the DTO.

void test_json_parse_min_cost_flat() {
    std::string json_str = R"({
        "enchantments": [
            {"id": "minecraft:sharpness", "name": "Sharpness", "max_level": 5, "multiplier": 1,
             "min_cost_base": 10, "min_cost_per_level": 7}
        ],
        "equipments": []
    })";

    auto result = NativeJsonParser::parse_string(json_str);
    const auto& enchantments = result.first;

    expect_eq(static_cast<int>(enchantments.size()), 1,
              "min_cost_flat: 1 enchantment");
    expect_eq(enchantments[0].min_cost_base, 10,
              "min_cost_flat: min_cost_base");
    expect_eq(enchantments[0].min_cost_per_level, 7,
              "min_cost_flat: min_cost_per_level");
    expect(enchantments[0].limited_level_provided == false,
           "min_cost_flat: no limited_level hint");

    std::cout << "PASS: test_json_parse_min_cost_flat" << std::endl;
}

// ─── test_json_parse_min_cost_nested ───────────────────────────────────
// B-T17: MC-nested min_cost object { base, per_level_above_first } populates
// the DTO.

void test_json_parse_min_cost_nested() {
    std::string json_str = R"({
        "enchantments": [
            {"id": "minecraft:sharpness", "name": "Sharpness", "max_level": 5, "multiplier": 1,
             "min_cost": {"base": 5, "per_level_above_first": 9}}
        ],
        "equipments": []
    })";

    auto result = NativeJsonParser::parse_string(json_str);
    const auto& enchantments = result.first;

    expect_eq(static_cast<int>(enchantments.size()), 1,
              "min_cost_nested: 1 enchantment");
    expect_eq(enchantments[0].min_cost_base, 5,
              "min_cost_nested: min_cost.base");
    expect_eq(enchantments[0].min_cost_per_level, 9,
              "min_cost_nested: min_cost.per_level_above_first");
    expect(enchantments[0].limited_level_provided == false,
           "min_cost_nested: no limited_level hint");

    std::cout << "PASS: test_json_parse_min_cost_nested" << std::endl;
}

// ─── test_json_parse_min_cost_default ──────────────────────────────────
// B-T17: neither min_cost nor limited_level → all default to 0, hint false.

void test_json_parse_min_cost_default() {
    std::string json_str = R"({
        "enchantments": [
            {"id": "minecraft:sharpness", "name": "Sharpness", "max_level": 5, "multiplier": 1}
        ],
        "equipments": []
    })";

    auto result = NativeJsonParser::parse_string(json_str);
    const auto& enchantments = result.first;

    expect_eq(static_cast<int>(enchantments.size()), 1,
              "min_cost_default: 1 enchantment");
    expect_eq(enchantments[0].min_cost_base, 0,
              "min_cost_default: base defaults 0");
    expect_eq(enchantments[0].min_cost_per_level, 0,
              "min_cost_default: per_level defaults 0");
    expect_eq(enchantments[0].limited_level, 0,
              "min_cost_default: limited_level defaults 0");
    expect(enchantments[0].limited_level_provided == false,
           "min_cost_default: no hint (fallback → max_level)");

    std::cout << "PASS: test_json_parse_min_cost_default" << std::endl;
}

// ─── test_json_parse_limited_level_hint ────────────────────────────────
// B-T17: legacy pre-computed `limited_level` field (no min_cost) → DTO keeps
// the value and marks limited_level_provided = true.

void test_json_parse_limited_level_hint() {
    std::string json_str = R"({
        "enchantments": [
            {"id": "minecraft:sharpness", "name": "Sharpness", "max_level": 5, "limited_level": 4, "multiplier": 1}
        ],
        "equipments": []
    })";

    auto result = NativeJsonParser::parse_string(json_str);
    const auto& enchantments = result.first;

    expect_eq(static_cast<int>(enchantments.size()), 1,
              "ll_hint: 1 enchantment");
    expect_eq(enchantments[0].limited_level, 4,
              "ll_hint: limited_level value kept");
    expect(enchantments[0].limited_level_provided == true,
           "ll_hint: limited_level_provided true");
    expect_eq(enchantments[0].min_cost_base, 0,
              "ll_hint: min_cost_base 0 (no min_cost)");
    expect_eq(enchantments[0].min_cost_per_level, 0,
              "ll_hint: min_cost_per_level 0 (no min_cost)");

    std::cout << "PASS: test_json_parse_limited_level_hint" << std::endl;
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

// ─── test_csv_parse_is_treasure ─────────────────────────────────────────
// B-T19: the is_treasure column (exported by EnchSerializer) is read back so a
// CSV round-trip preserves the treasure flag instead of defaulting to false.

void test_csv_parse_is_treasure() {
    std::string csv =
        "id,name,max_level,limited_level,multiplier,is_treasure,exclusive_set,supported_items\n"
        "minecraft:mending,Mending,1,1,4,true,infinity,#minecraft:durability\n"
        "minecraft:sharpness,Sharpness,5,5,1,false,,\n";

    auto enchantments = NativeCsvParser::parse(csv);

    expect_eq(static_cast<int>(enchantments.size()), 2,
              "csv_treasure: 2 enchantments");
    expect(enchantments[0].is_treasure, "csv_treasure: mending is_treasure true");
    expect(!enchantments[1].is_treasure, "csv_treasure: sharpness is_treasure false");

    TEST_PASS("test_csv_parse_is_treasure");
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
// B-T18: the parser no longer computes limited_level — that moved to the
// registry-level LimitedLevelCalculator. parse_single_enchantment only
// carries the raw min_cost fields and, when the JSON has no `limited_level`
// field, defaults the DTO's limited_level to max_level (the calculator
// back-fills the real value at registry load). This pins the parser's
// current behavior: raw pass-through of supported_items and raw min_cost.

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

    // No `limited_level` field in the JSON → DTO defaults to max_level.
    expect_eq(ench.limited_level, ench.max_level,
              "mc_limited_tag: limited_level defaults to max_level");
    expect(!ench.limited_level_provided,
           "mc_limited_tag: limited_level not marked provided");

    // min_cost is carried as raw fields for the LimitedLevelCalculator.
    expect_eq(ench.min_cost_base, 10,
              "mc_limited_tag: min_cost_base carried raw");
    expect_eq(ench.min_cost_per_level, 5,
              "mc_limited_tag: min_cost_per_level carried raw");

    TEST_PASS("test_mc_limited_level_tag_resolved");
}

// ─── test_mc_single_enchantment_treasure_tag ───────────────────────────
// B-T19: is_treasure is derived from `#minecraft:enchantment/treasure` tag
// membership (the datapack parser seeds the vanilla tag universe, so the
// canonical full-path key resolves).  A member gets is_treasure=true; a
// non-member stays false.

void test_mc_single_enchantment_treasure_tag() {
    TagResolver tag_resolver;
    tag_resolver.load_tag_content("minecraft:enchantment/treasure",
        R"({"values": ["minecraft:mending"]})");

    std::string mending = R"({
        "anvil_cost": 4,
        "max_level": 1,
        "supported_items": "#minecraft:durability"
    })";
    auto ench = McOfficialParser::parse_single_enchantment(
        "minecraft", "mending", mending, tag_resolver);
    expect(ench.is_treasure, "mending: treasure member → is_treasure true");

    std::string sharpness = R"({
        "anvil_cost": 1,
        "max_level": 5,
        "supported_items": "#minecraft:sharp_weapon"
    })";
    auto sharp = McOfficialParser::parse_single_enchantment(
        "minecraft", "sharpness", sharpness, tag_resolver);
    expect(!sharp.is_treasure, "sharpness: not a treasure member → is_treasure false");

    TEST_PASS("test_mc_single_enchantment_treasure_tag");
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
    const auto& enchantments = result.enchantments;
    const auto& equipment    = result.equipment;

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
    const auto& enchantments = result.enchantments;
    const auto& equipment    = result.equipment;

    expect(enchantments.empty(), "mc_files_empty: no enchantments");
    expect(equipment.empty(),    "mc_files_empty: no equipment");

    TEST_PASS("test_mc_parse_files_empty");
}

// ─── test_mc_official_single_string_supported ──────────────────────────
// T10: real MC 1.21+ datapack format allows supported_items to be a SINGLE
// STRING (e.g. "#minecraft:swords") rather than an array. Regression: the
// array-only parser threw JsonException on a string. Fixture (copied from the
// git-ignored res/More Enchants 1.4/...): data/tests/datapack/attack_speed.json.
// Verify it does NOT throw and the raw tag reference survives in applicable_to.

void test_mc_official_single_string_supported() {
    TagResolver tag_resolver;

    std::string content = file_utils::read_file(
        "data/tests/datapack/attack_speed.json");

    auto ench = McOfficialParser::parse_single_enchantment(
        "enchantments", "attack_speed", content, tag_resolver);

    expect_eq(ench.id, std::string("enchantments:attack_speed"),
              "mc_single_string: ench id");
    expect_eq(ench.multiplier, 7,
              "mc_single_string: anvil_cost -> multiplier");
    expect_eq(ench.max_level, 3,
              "mc_single_string: max_level");

    // supported_items was a single string "#minecraft:swords" — passed through RAW
    bool has_swords = false;
    for (const auto& a : ench.applicable_to) {
        if (a == "#minecraft:swords") { has_swords = true; break; }
    }
    expect(has_swords,
           "mc_single_string: applicable_to contains raw #minecraft:swords");

    TEST_PASS("test_mc_official_single_string_supported");
}

// ─── test_mc_official_array_supported_items ────────────────────────────
// T10: real MC datapack also uses supported_items as an ARRAY of concrete
// item IDs (no "#" prefix). Fixture (copied from the git-ignored
// res/enchantments-encore-4.6/...): data/tests/datapack/moonwalk.json.
// Verify the array form still parses and concrete IDs pass through unchanged.

void test_mc_official_array_supported_items() {
    TagResolver tag_resolver;

    std::string content = file_utils::read_file(
        "data/tests/datapack/moonwalk.json");

    auto ench = McOfficialParser::parse_single_enchantment(
        "enchantencore", "moonwalk", content, tag_resolver);

    expect_eq(ench.id, std::string("enchantencore:moonwalk"),
              "mc_array_supp: ench id");
    expect_eq(ench.multiplier, 4,
              "mc_array_supp: anvil_cost -> multiplier");
    expect_eq(ench.max_level, 3,
              "mc_array_supp: max_level");

    bool has_elytra = false;
    for (const auto& a : ench.applicable_to) {
        if (a == "minecraft:elytra") { has_elytra = true; break; }
    }
    expect(has_elytra,
           "mc_array_supp: applicable_to contains minecraft:elytra");

    TEST_PASS("test_mc_official_array_supported_items");
}

// ─── test_mc_single_string_exclusive_set ───────────────────────────────
// T10: exclusive_set may also be a single string reference (robustness of the
// collect_strings helper). Empty TagResolver → tag resolves to nothing, but
// parsing must NOT throw.

void test_mc_single_string_exclusive_set() {
    TagResolver tag_resolver;

    std::string content = R"({
        "anvil_cost": 1,
        "max_level": 5,
        "exclusive_set": "#minecraft:sword",
        "supported_items": "#minecraft:sword"
    })";

    auto ench = McOfficialParser::parse_single_enchantment(
        "minecraft", "sharpness", content, tag_resolver);

    expect_eq(ench.id, std::string("minecraft:sharpness"),
              "mc_single_excl: ench id");
    expect(ench.exclusive_with.empty(),
           "mc_single_excl: unresolved tag resolves to empty exclusive_with");

    TEST_PASS("test_mc_single_string_exclusive_set");
}

// ─── test_mc_tag_replace_semantics ─────────────────────────────────────
// T10: MC 1.21 tag files may carry "replace": true → the definition REPLACES
// any existing tag with the same key instead of merging. Absent / false → the
// definitions MERGE (datapack semantics).

void test_mc_tag_replace_semantics() {
    TagResolver resolver;

    // First definition for the tag
    resolver.load_tag_content("minecraft:swords",
        R"({"values": ["minecraft:stone_sword", "minecraft:wooden_sword"]})");

    // Without "replace", a second definition MERGES with the first
    resolver.load_tag_content("minecraft:swords",
        R"({"values": ["minecraft:iron_sword"]})");
    auto merged = resolver.resolve("#minecraft:swords");
    expect(merged.contains("minecraft:stone_sword"),
           "tag_replace: merge keeps first tag's values");
    expect(merged.contains("minecraft:iron_sword"),
           "tag_replace: merge adds second tag's values");

    // With "replace": true, the tag is replaced entirely
    resolver.load_tag_content("minecraft:swords",
        R"({"replace": true, "values": ["minecraft:diamond_sword"]})");
    auto replaced = resolver.resolve("#minecraft:swords");
    expect_eq(static_cast<int>(replaced.size()), 1,
              "tag_replace: replaced tag has only the new value");
    expect(replaced.contains("minecraft:diamond_sword"),
           "tag_replace: replaced tag contains the new value");

    TEST_PASS("test_mc_tag_replace_semantics");
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
        test_json_parse_min_cost_flat();
        test_json_parse_min_cost_nested();
        test_json_parse_min_cost_default();
        test_json_parse_limited_level_hint();
        test_json_parse_empty();
        test_json_parse_via_Json();

        // Section B — NativeCsvParser
        test_csv_parse_basic();
        test_csv_parse_with_exclusive();
        test_csv_parse_empty_header_only();
        test_csv_parse_multiple_rows();
        test_csv_parse_is_treasure();

        // Section C — McOfficialParser
        test_mc_single_enchantment_basic();
        test_mc_single_enchantment_with_exclusive();
        test_mc_limited_level_tag_resolved();
        test_mc_single_enchantment_treasure_tag();
        test_mc_parse_files_basic();
        test_mc_parse_files_empty();
        test_mc_official_single_string_supported();
        test_mc_official_array_supported_items();
        test_mc_single_string_exclusive_set();
        test_mc_tag_replace_semantics();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
