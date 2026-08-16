#define BESQ_TEST_MAIN
#include "framework/test_framework.h"

#include "common/io/FileUtils.hpp"
#include "domain/business/components/FormatDetector.h"
#include "domain/business/components/TagResolver.h"
#include "domain/business/parsers/McOfficialParser.h"
#include "domain/business/parsers/NativeCsvParser.h"
#include "domain/business/parsers/NativeJsonParser.h"
#include "domain/business/types/dto/EnchantmentData.h"
#include "domain/business/types/dto/EquipmentData.h"

#include <filesystem>
#include <fstream>
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

TEST_CASE("test_json_parse_basic") {
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
    const auto& equipment = result.second;

    // Enchantment checks
    expect_eq(static_cast<int>(enchantments.size()), 1, "json_basic: 1 enchantment");
    expect_eq(enchantments[0].id, std::string("minecraft:sharpness"), "json_basic: ench id");
    expect_eq(enchantments[0].display_name, std::string("Sharpness"), "json_basic: ench display_name");
    expect_eq(enchantments[0].max_level, 5, "json_basic: ench max_level");

    // Equipment checks
    expect_eq(static_cast<int>(equipment.size()), 1, "json_basic: 1 equipment");
    expect_eq(equipment[0].id, std::string("minecraft:diamond_sword"), "json_basic: eq id");
    expect_eq(equipment[0].display_name, std::string("Diamond Sword"), "json_basic: eq display_name");
    expect_eq(equipment[0].category, std::string("sword"), "json_basic: eq category");
}

// ─── test_json_parse_with_exclusive ────────────────────────────────────
// Include exclusive_set field. Concrete IDs (no "#" prefix) pass through
// resolve_references unchanged. Verify the value is present.

TEST_CASE("test_json_parse_with_exclusive") {
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

    expect_eq(static_cast<int>(enchantments.size()), 1, "json_exclusive: 1 enchantment");
    expect_eq(enchantments[0].id, std::string("minecraft:sharpness"), "json_exclusive: ench id");

    // exclusive_set value "minecraft:smite" has no '#' prefix, so
    // TagResolver::resolve() returns it as-is.
    bool has_smite = false;
    for (const auto& excl : enchantments[0].exclusive_with) {
        if (excl == "minecraft:smite") {
            has_smite = true;
            break;
        }
    }
    expect(has_smite, "json_exclusive: exclusive_with contains smite");
}

// ─── test_json_parse_with_applicable ───────────────────────────────────
// Include supported_items (the T2/T10 field name). The value "#minecraft:sword"
// is a tag reference passed through raw. Verify parsing completes without error.

TEST_CASE("test_json_parse_with_applicable") {
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

    expect_eq(static_cast<int>(enchantments.size()), 1, "json_applicable: 1 enchantment");
    expect_eq(enchantments[0].id, std::string("minecraft:sharpness"), "json_applicable: ench id");

    // The supported_items field is passed through RAW (T5/T10): the
    // "#minecraft:sword" tag reference is preserved verbatim, not expanded by
    // the parser. The loader (T6) performs the cross-validation later.
    bool has_tag = false;
    for (const auto& a : enchantments[0].applicable_to) {
        if (a == "#minecraft:sword") {
            has_tag = true;
            break;
        }
    }
    expect(has_tag, "json_applicable: applicable_to contains raw #minecraft:sword");
}

// ─── test_json_parse_min_cost_flat ─────────────────────────────────────
// B-T17: flat min_cost_base / min_cost_per_level fields populate the DTO.

TEST_CASE("test_json_parse_min_cost_flat") {
    std::string json_str = R"({
        "enchantments": [
            {"id": "minecraft:sharpness", "name": "Sharpness", "max_level": 5, "multiplier": 1,
             "min_cost_base": 10, "min_cost_per_level": 7}
        ],
        "equipments": []
    })";

    auto result = NativeJsonParser::parse_string(json_str);
    const auto& enchantments = result.first;

    expect_eq(static_cast<int>(enchantments.size()), 1, "min_cost_flat: 1 enchantment");
    expect_eq(enchantments[0].min_cost_base, 10, "min_cost_flat: min_cost_base");
    expect_eq(enchantments[0].min_cost_per_level, 7, "min_cost_flat: min_cost_per_level");
    expect(enchantments[0].limited_level_provided == false, "min_cost_flat: no limited_level hint");
}

// ─── test_json_parse_min_cost_nested ───────────────────────────────────
// B-T17: MC-nested min_cost object { base, per_level_above_first } populates
// the DTO.

TEST_CASE("test_json_parse_min_cost_nested") {
    std::string json_str = R"({
        "enchantments": [
            {"id": "minecraft:sharpness", "name": "Sharpness", "max_level": 5, "multiplier": 1,
             "min_cost": {"base": 5, "per_level_above_first": 9}}
        ],
        "equipments": []
    })";

    auto result = NativeJsonParser::parse_string(json_str);
    const auto& enchantments = result.first;

    expect_eq(static_cast<int>(enchantments.size()), 1, "min_cost_nested: 1 enchantment");
    expect_eq(enchantments[0].min_cost_base, 5, "min_cost_nested: min_cost.base");
    expect_eq(enchantments[0].min_cost_per_level, 9, "min_cost_nested: min_cost.per_level_above_first");
    expect(enchantments[0].limited_level_provided == false, "min_cost_nested: no limited_level hint");
}

// ─── test_json_parse_min_cost_default ──────────────────────────────────
// B-T17: neither min_cost nor limited_level → all default to 0, hint false.

TEST_CASE("test_json_parse_min_cost_default") {
    std::string json_str = R"({
        "enchantments": [
            {"id": "minecraft:sharpness", "name": "Sharpness", "max_level": 5, "multiplier": 1}
        ],
        "equipments": []
    })";

    auto result = NativeJsonParser::parse_string(json_str);
    const auto& enchantments = result.first;

    expect_eq(static_cast<int>(enchantments.size()), 1, "min_cost_default: 1 enchantment");
    expect_eq(enchantments[0].min_cost_base, 0, "min_cost_default: base defaults 0");
    expect_eq(enchantments[0].min_cost_per_level, 0, "min_cost_default: per_level defaults 0");
    expect_eq(enchantments[0].limited_level, 0, "min_cost_default: limited_level defaults 0");
    expect(enchantments[0].limited_level_provided == false, "min_cost_default: no hint (fallback → max_level)");
}

// ─── test_json_parse_limited_level_hint ────────────────────────────────
// B-T17: legacy pre-computed `limited_level` field (no min_cost) → DTO keeps
// the value and marks limited_level_provided = true.

TEST_CASE("test_json_parse_limited_level_hint") {
    std::string json_str = R"({
        "enchantments": [
            {"id": "minecraft:sharpness", "name": "Sharpness", "max_level": 5, "limited_level": 4, "multiplier": 1}
        ],
        "equipments": []
    })";

    auto result = NativeJsonParser::parse_string(json_str);
    const auto& enchantments = result.first;

    expect_eq(static_cast<int>(enchantments.size()), 1, "ll_hint: 1 enchantment");
    expect_eq(enchantments[0].limited_level, 4, "ll_hint: limited_level value kept");
    expect(enchantments[0].limited_level_provided == true, "ll_hint: limited_level_provided true");
    expect_eq(enchantments[0].min_cost_base, 0, "ll_hint: min_cost_base 0 (no min_cost)");
    expect_eq(enchantments[0].min_cost_per_level, 0, "ll_hint: min_cost_per_level 0 (no min_cost)");
}

// ─── test_json_parse_platform ──────────────────────────────────────────
// T4: the native JSON parser reads the `platform` field into the DTO.
// Primary key `platform`, legacy alias `supported_platform` both accepted.

TEST_CASE("test_json_parse_platform") {
    std::string json_str = R"({
        "enchantments": [
            {"id": "minecraft:sharpness", "name": "Sharpness", "platform": "java",
             "max_level": 5, "multiplier": 1}
        ],
        "equipments": []
    })";
    auto result = NativeJsonParser::parse_string(json_str);
    expect(result.first[0].platform == "java", "native JSON platform read into DTO");
    // 旧别名 supported_platform
    std::string legacy = R"({
        "enchantments": [
            {"id": "minecraft:mending", "name": "Mending", "supported_platform": "bedrock",
             "max_level": 1, "multiplier": 4}
        ],
        "equipments": []
    })";
    auto r2 = NativeJsonParser::parse_string(legacy);
    expect(r2.first[0].platform == "bedrock", "legacy supported_platform alias read");
    TEST_PASS("test_json_parse_platform");
}

// ─── test_json_parse_malformed_tolerated ──────────────────────────────
// T4: 新容错语义。旧行为：max_level 为字符串抛 JsonException 中止整次解析；
// 新行为：schema 记录错误 → WARN → 该条目跳过，其余条目继续（容错）。

TEST_CASE("test_json_parse_malformed_tolerated") {
    std::string json_str = R"({
        "enchantments": [
            {"id": "minecraft:bad", "name": "Bad", "max_level": "five", "multiplier": 1},
            {"id": "minecraft:sharpness", "name": "Sharpness", "max_level": 5, "multiplier": 1}
        ],
        "equipments": []
    })";
    auto result = NativeJsonParser::parse_string(json_str);
    // malformed 条目被跳过，合法条目保留——解析不抛异常
    expect_eq(static_cast<int>(result.first.size()), 1, "malformed_tolerated: malformed entry dropped, valid kept");
    expect_eq(result.first[0].id, std::string("minecraft:sharpness"), "malformed_tolerated: valid entry retained");
    TEST_PASS("test_json_parse_malformed_tolerated");
}

// ─── test_json_parse_empty ─────────────────────────────────────────────
// parse_string("{}"). Verify empty results, no crash.

TEST_CASE("test_json_parse_empty") {
    auto result = NativeJsonParser::parse_string("{}");
    const auto& enchantments = result.first;
    const auto& equipment = result.second;

    expect(enchantments.empty(), "json_empty: no enchantments");
    expect(equipment.empty(), "json_empty: no equipment");
}

// ─── test_json_parse_via_Json ──────────────────────────────────────────
// Construct a Json DOM manually using the builder API (Json::object(),
// Json::array(), set(), push_back()), then call parse(json). Verify results.

TEST_CASE("test_json_parse_via_Json") {
    // Build the JSON DOM manually
    Json root = Json::object();

    // -- Enchantments array --
    Json enchs = Json::array();
    {
        Json ench = Json::object();
        ench.set("id", Json("minecraft:sharpness"));
        ench.set("name", Json("Sharpness"));
        ench.set("max_level", Json(5));
        ench.set("limited_level", Json(5));
        ench.set("multiplier", Json(1));
        enchs.push_back(ench);
    }
    root.set("enchantments", enchs);

    // -- Equipments array --
    Json eqs = Json::array();
    {
        Json eq = Json::object();
        eq.set("id", Json("minecraft:diamond_sword"));
        eq.set("name", Json("Diamond Sword"));
        eq.set("category", Json("sword"));
        eq.set("max_durability", Json(1561));
        eqs.push_back(eq);
    }
    root.set("equipments", eqs);

    // Parse via the Json DOM overload
    auto result = NativeJsonParser::parse(root);
    const auto& enchantments = result.first;
    const auto& equipment = result.second;

    // Enchantment checks
    expect_eq(static_cast<int>(enchantments.size()), 1, "json_manual: 1 enchantment");
    expect_eq(enchantments[0].id, std::string("minecraft:sharpness"), "json_manual: ench id");
    expect_eq(enchantments[0].display_name, std::string("Sharpness"), "json_manual: ench display_name");
    expect_eq(enchantments[0].max_level, 5, "json_manual: ench max_level");

    // Equipment checks
    expect_eq(static_cast<int>(equipment.size()), 1, "json_manual: 1 equipment");
    expect_eq(equipment[0].id, std::string("minecraft:diamond_sword"), "json_manual: eq id");
    expect_eq(equipment[0].display_name, std::string("Diamond Sword"), "json_manual: eq display_name");
    expect_eq(equipment[0].category, std::string("sword"), "json_manual: eq category");
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

TEST_CASE("test_csv_parse_basic") {
    std::string csv = "id,name,max_level,limited_level,multiplier,exclusive_set,supported_items\n"
                      "minecraft:sharpness,Sharpness,5,5,1,,\"#minecraft:sword\"\n";

    auto enchantments = NativeCsvParser::parse(csv);

    expect_eq(static_cast<int>(enchantments.size()), 1, "csv_basic: 1 enchantment");
    expect_eq(enchantments[0].id, std::string("minecraft:sharpness"), "csv_basic: ench id");
    expect_eq(enchantments[0].display_name, std::string("Sharpness"), "csv_basic: ench display_name");
    expect_eq(enchantments[0].max_level, 5, "csv_basic: ench max_level");
    expect_eq(enchantments[0].multiplier, 1, "csv_basic: ench multiplier");
}

// ─── test_csv_parse_with_exclusive ─────────────────────────────────────
// Include exclusive_set column with a concrete ID. Note the CSV format
// uses semicolons as delimiters inside the exclusive_set cell; a single
// value does not need a semicolon.

TEST_CASE("test_csv_parse_with_exclusive") {
    std::string csv = "id,name,max_level,limited_level,multiplier,exclusive_set,supported_items\n"
                      "minecraft:sharpness,Sharpness,5,5,1,minecraft:smite,#minecraft:sword\n";

    auto enchantments = NativeCsvParser::parse(csv);

    expect_eq(static_cast<int>(enchantments.size()), 1, "csv_exclusive: 1 enchantment");
    expect_eq(enchantments[0].id, std::string("minecraft:sharpness"), "csv_exclusive: ench id");

    bool has_smite = false;
    for (const auto& excl : enchantments[0].exclusive_with) {
        if (excl == "minecraft:smite") {
            has_smite = true;
            break;
        }
    }
    expect(has_smite, "csv_exclusive: exclusive_with contains smite");
}

// ─── test_csv_parse_empty_header_only ──────────────────────────────────
// Only the header row — no data rows. Verify empty result.

TEST_CASE("test_csv_parse_empty_header_only") {
    std::string csv = "id,name,max_level,limited_level,multiplier,exclusive_set,supported_items\n";

    auto enchantments = NativeCsvParser::parse(csv);

    expect(enchantments.empty(), "csv_empty_header: no enchantments");
}

// ─── test_csv_parse_multiple_rows ──────────────────────────────────────
// Three data rows. Verify all are parsed and have correct IDs.

TEST_CASE("test_csv_parse_multiple_rows") {
    std::string csv = "id,name,max_level,limited_level,multiplier,exclusive_set,supported_items\n"
                      "minecraft:sharpness,Sharpness,5,5,1,,\n"
                      "minecraft:smite,Smite,5,5,1,,\n"
                      "minecraft:unbreaking,Unbreaking,3,3,1,,\n";

    auto enchantments = NativeCsvParser::parse(csv);

    expect_eq(static_cast<int>(enchantments.size()), 3, "csv_multi: 3 enchantments");
    expect_eq(enchantments[0].id, std::string("minecraft:sharpness"), "csv_multi: ench[0].id");
    expect_eq(enchantments[1].id, std::string("minecraft:smite"), "csv_multi: ench[1].id");
    expect_eq(enchantments[2].id, std::string("minecraft:unbreaking"), "csv_multi: ench[2].id");
}

// ─── test_csv_parse_is_treasure ─────────────────────────────────────────
// B-T19: the is_treasure column (exported by EnchSerializer) is read back so a
// CSV round-trip preserves the treasure flag instead of defaulting to false.

TEST_CASE("test_csv_parse_is_treasure") {
    std::string csv = "id,name,max_level,limited_level,multiplier,is_treasure,exclusive_set,supported_items\n"
                      "minecraft:mending,Mending,1,1,4,true,infinity,#minecraft:durability\n"
                      "minecraft:sharpness,Sharpness,5,5,1,false,,\n";

    auto enchantments = NativeCsvParser::parse(csv);

    expect_eq(static_cast<int>(enchantments.size()), 2, "csv_treasure: 2 enchantments");
    expect(enchantments[0].is_treasure, "csv_treasure: mending is_treasure true");
    expect(!enchantments[1].is_treasure, "csv_treasure: sharpness is_treasure false");

    TEST_PASS("test_csv_parse_is_treasure");
}

// ─── test_csv_parse_platform ───────────────────────────────────────────
// The schema-driven parser reads the platform / min_cost columns that the
// EnchSerializer emits, so a CSV round-trip preserves them instead of
// defaulting.

TEST_CASE("test_csv_parse_platform") {
    std::string csv = "id,name,platform,max_level,limited_level,min_cost_base,min_cost_per_level,multiplier,is_treasure,"
                      "exclusive_set,supported_items\n"
                      "minecraft:sharpness,Sharpness,java,5,5,1,11,1,false,,\"#minecraft:sword\"\n"
                      "minecraft:mending,Mending,all,1,1,2,5,4,true,,\"#minecraft:durability\"\n";
    auto enchantments = NativeCsvParser::parse(csv);
    expect_eq(static_cast<int>(enchantments.size()), 2, "csv_platform: 2 enchants");
    expect(enchantments[0].platform == "java", "csv_platform: platform column read");
    expect(enchantments[1].platform == "all", "csv_platform: 'all' read");
    expect(enchantments[1].is_treasure, "csv_platform: is_treasure read");
    expect_eq(enchantments[0].min_cost_base, 1, "csv_platform: min_cost_base read");
    TEST_PASS("test_csv_parse_platform");
}

// ─── test_csv_parse_escaped_quotes ─────────────────────────────────────
// 旧 parse_csv_string 不处理 "" 转义——回归：逗号字段用引号包裹，内含引号转义为 ""

TEST_CASE("test_csv_parse_escaped_quotes") {
    std::string csv = "id,name,max_level,limited_level,multiplier,exclusive_set,supported_items\n"
                      "\"minecraft:sharpness\",\"Sharp, Comma\",5,5,1,,\"#minecraft:sword\"\n"
                      "\"minecraft:unbreaking\",\"Un\"\"broken\"\"\",3,3,1,,\n";
    auto enchantments = NativeCsvParser::parse(csv);
    expect_eq(static_cast<int>(enchantments.size()), 2, "csv_escaped: 2 enchants");
    expect(enchantments[0].display_name == "Sharp, Comma", "csv_escaped: comma inside quotes");
    expect(enchantments[1].display_name == "Un\"broken\"", "csv_escaped: escaped double-quote");
    TEST_PASS("test_csv_parse_escaped_quotes");
}

// ─── test_csv_parse_equipment_companion ────────────────────────────────
// Equipment companion CSV (equipments_<stem>.csv) is parsed via the
// EquipmentDataSchema.

TEST_CASE("test_csv_parse_equipment_companion") {
    std::string csv = "id,name,category,max_durability\n"
                      "minecraft:diamond_sword,Diamond Sword,sword,1561\n"
                      "minecraft:iron_pickaxe,Iron Pickaxe,pickaxe,250\n";
    auto eqs = NativeCsvParser::parse_equipment(csv);
    expect_eq(static_cast<int>(eqs.size()), 2, "csv_eq: 2 equipments");
    expect_eq(eqs[0].id, std::string("minecraft:diamond_sword"), "csv_eq: id");
    expect_eq(eqs[0].category, std::string("sword"), "csv_eq: category");
    expect_eq(eqs[0].max_durability, 1561, "csv_eq: max_durability");
    TEST_PASS("test_csv_parse_equipment_companion");
}

// ─── test_csv_empty_scalar_cell_drops_row ─────────────────────────────
// 旧解析器：空 limited_level 单元格回退 max_level 保留行；空 is_treasure 默认
// false 保留行。新引擎 CSV 契约（test_csv_presence_zero_counts_as_present 固化）：
// 空数值格 = codec 错误 → parse_row 失败 → 该行丢弃（WARN）。向后兼容仅承诺
// "缺列"（缺失可选字段默认），不承诺"列在但空"。本测试锁定此刻意行为。

TEST_CASE("test_csv_empty_scalar_cell_drops_row") {
    std::string csv = "id,name,max_level,limited_level,multiplier,is_treasure,exclusive_set,supported_items\n"
                      "minecraft:bad_empty,Bad,5,,1,,,\n"              // limited_level 空 + is_treasure 空 → 行丢弃
                      "minecraft:sharpness,Sharpness,5,5,1,false,,\n"; // 合法行保留
    auto enchantments = NativeCsvParser::parse(csv);
    expect_eq(static_cast<int>(enchantments.size()), 1, "empty_cell: malformed row dropped, valid kept");
    expect_eq(enchantments[0].id, std::string("minecraft:sharpness"), "empty_cell: valid row retained");
    TEST_PASS("test_csv_empty_scalar_cell_drops_row");
}

// ─── test_format_detector_csv_companion ─────────────────────────────────
// T6: FormatDetector::parse on a NativeCsv file reads back the companion
// equipment file (equipments_<stem>.csv).  The companion is merged into the
// parse result's `equipment` vector; without a companion the vector stays
// empty.

TEST_CASE("test_format_detector_csv_companion") {
    namespace fs = std::filesystem;
    auto dir = fs::temp_directory_path() / "besq_csv_comp";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "custom.csv") << "id,name,platform,max_level,multiplier,exclusive_set,supported_items\n"
                                         "mod:sharp,Sharp,java,5,1,,\"#minecraft:swords\"\n";
    std::ofstream(dir / "equipments_custom.csv") << "id,name,category,max_durability\n"
                                                    "minecraft:diamond_sword,Diamond Sword,sword,1561\n";
    auto result = FormatDetector::parse(dir / "custom.csv");
    expect_eq(static_cast<int>(result.enchantments.size()), 1, "csv_comp: 1 ench");
    expect_eq(static_cast<int>(result.equipment.size()), 1, "csv_comp: 1 eq from companion");
    expect(result.equipment[0].id == "minecraft:diamond_sword", "csv_comp: eq id");
    expect(result.enchantments[0].platform == "java", "csv_comp: platform read");

    // Negative: a CSV WITHOUT a companion file → empty equipment vector.
    std::ofstream(dir / "naked.csv") << "id,name,max_level,multiplier\n"
                                        "mod:plain,Plain,1,1\n";
    auto no_comp = FormatDetector::parse(dir / "naked.csv");
    expect_eq(static_cast<int>(no_comp.equipment.size()), 0, "csv_comp: no companion → empty equipment");

    fs::remove_all(dir);
    TEST_PASS("test_format_detector_csv_companion");
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

TEST_CASE("test_mc_single_enchantment_basic") {
    TagResolver tag_resolver;

    std::string content = R"({
        "anvil_cost": 1,
        "max_level": 5,
        "exclusive_set": [],
        "supported_items": ["#minecraft:sword"]
    })";

    auto ench = McOfficialParser::parse_single_enchantment("minecraft", "sharpness", content, tag_resolver);

    expect_eq(ench.id, std::string("minecraft:sharpness"), "mc_single_basic: ench id");
    expect_eq(ench.max_level, 5, "mc_single_basic: max_level");
    expect_eq(ench.display_name, std::string("Sharpness"), "mc_single_basic: display_name");
    expect(ench.multiplier > 0, "mc_single_basic: multiplier > 0");

    // applicable_to is passed through RAW (T5): the "#minecraft:sword" tag
    // reference survives verbatim for the loader to resolve later.
    bool has_tag = false;
    for (const auto& a : ench.applicable_to) {
        if (a == "#minecraft:sword") {
            has_tag = true;
            break;
        }
    }
    expect(has_tag, "mc_single_basic: applicable_to contains raw #minecraft:sword");

    // exclusive_set should be empty
    expect(ench.exclusive_with.empty(), "mc_single_basic: exclusive_with empty");

    TEST_PASS("test_mc_single_enchantment_basic");
}

// ─── test_mc_single_enchantment_with_exclusive ─────────────────────────
// Parse an enchantment with a non-empty exclusive_set containing a concrete
// enchantment ID ("minecraft:smite"). Also verify concrete supported_items
// pass through resolve() unchanged.

TEST_CASE("test_mc_single_enchantment_with_exclusive") {
    TagResolver tag_resolver;

    std::string content = R"({
        "anvil_cost": 2,
        "max_level": 5,
        "exclusive_set": ["minecraft:smite"],
        "supported_items": ["minecraft:diamond_sword"]
    })";

    auto ench = McOfficialParser::parse_single_enchantment("minecraft", "sharpness", content, tag_resolver);

    expect_eq(ench.id, std::string("minecraft:sharpness"), "mc_single_exclusive: ench id");
    expect_eq(ench.max_level, 5, "mc_single_exclusive: max_level");
    expect_eq(ench.multiplier, 2, "mc_single_exclusive: multiplier");

    // exclusive_set should contain "minecraft:smite"
    bool has_smite = false;
    for (const auto& e : ench.exclusive_with) {
        if (e == "minecraft:smite") {
            has_smite = true;
            break;
        }
    }
    expect(has_smite, "mc_single_exclusive: exclusive_with contains smite");

    // Concrete supported_items pass through resolve() unchanged
    bool has_sword = false;
    for (const auto& a : ench.applicable_to) {
        if (a == "minecraft:diamond_sword") {
            has_sword = true;
            break;
        }
    }
    expect(has_sword, "mc_single_exclusive: applicable_to contains diamond_sword");

    TEST_PASS("test_mc_single_enchantment_with_exclusive");
}

// ─── test_mc_limited_level_tag_resolved ────────────────────────────────
// B-T18: the parser no longer computes limited_level — that moved to the
// registry-level LimitedLevelCalculator. parse_single_enchantment only
// carries the raw min_cost fields and, when the JSON has no `limited_level`
// field, defaults the DTO's limited_level to max_level (the calculator
// back-fills the real value at registry load). This pins the parser's
// current behavior: raw pass-through of supported_items and raw min_cost.

TEST_CASE("test_mc_limited_level_tag_resolved") {
    TagResolver tag_resolver;
    tag_resolver.load_tag_content("minecraft:sword", R"({"values": ["minecraft:diamond_sword", "minecraft:iron_sword"]})");

    std::string content = R"({
        "anvil_cost": 1,
        "max_level": 5,
        "exclusive_set": [],
        "supported_items": ["#minecraft:sword"],
        "min_cost": {"base": 10, "per_level_above_first": 5}
    })";

    auto ench = McOfficialParser::parse_single_enchantment("minecraft", "sharpness", content, tag_resolver);

    // applicable_to stays raw (pass-through, T5)
    bool has_tag = false;
    for (const auto& a : ench.applicable_to) {
        if (a == "#minecraft:sword") {
            has_tag = true;
            break;
        }
    }
    expect(has_tag, "mc_limited_tag: applicable_to raw #minecraft:sword");

    // No `limited_level` field in the JSON → DTO defaults to max_level.
    expect_eq(ench.limited_level, ench.max_level, "mc_limited_tag: limited_level defaults to max_level");
    expect(!ench.limited_level_provided, "mc_limited_tag: limited_level not marked provided");

    // min_cost is carried as raw fields for the LimitedLevelCalculator.
    expect_eq(ench.min_cost_base, 10, "mc_limited_tag: min_cost_base carried raw");
    expect_eq(ench.min_cost_per_level, 5, "mc_limited_tag: min_cost_per_level carried raw");

    TEST_PASS("test_mc_limited_level_tag_resolved");
}

// ─── test_mc_single_enchantment_treasure_tag ───────────────────────────
// B-T19: is_treasure is derived from `#minecraft:enchantment/treasure` tag
// membership (the datapack parser seeds the vanilla tag universe, so the
// canonical full-path key resolves).  A member gets is_treasure=true; a
// non-member stays false.

TEST_CASE("test_mc_single_enchantment_treasure_tag") {
    TagResolver tag_resolver;
    tag_resolver.load_tag_content("minecraft:enchantment/treasure", R"({"values": ["minecraft:mending"]})");

    std::string mending = R"({
        "anvil_cost": 4,
        "max_level": 1,
        "supported_items": "#minecraft:durability"
    })";
    auto ench = McOfficialParser::parse_single_enchantment("minecraft", "mending", mending, tag_resolver);
    expect(ench.is_treasure, "mending: treasure member → is_treasure true");

    std::string sharpness = R"({
        "anvil_cost": 1,
        "max_level": 5,
        "supported_items": "#minecraft:sharp_weapon"
    })";
    auto sharp = McOfficialParser::parse_single_enchantment("minecraft", "sharpness", sharpness, tag_resolver);
    expect(!sharp.is_treasure, "sharpness: not a treasure member → is_treasure false");

    TEST_PASS("test_mc_single_enchantment_treasure_tag");
}

// ─── test_mc_parse_files_basic ─────────────────────────────────────────
// Use parse_files() with a map containing one enchantment file and one
// item tag file. Verify the enchantment is parsed and equipment is derived
// from the tag's item IDs.

TEST_CASE("test_mc_parse_files_basic") {
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
    const auto& equipment = result.equipment;

    expect_eq(static_cast<int>(enchantments.size()), 1, "mc_files_basic: 1 enchantment");
    expect_eq(enchantments[0].id, std::string("minecraft:sharpness"), "mc_files_basic: ench id");
    expect_eq(enchantments[0].max_level, 5, "mc_files_basic: max_level");

    expect_eq(static_cast<int>(equipment.size()), 1, "mc_files_basic: 1 equipment");
    expect_eq(equipment[0].id, std::string("minecraft:diamond_sword"), "mc_files_basic: eq id");

    TEST_PASS("test_mc_parse_files_basic");
}

// ─── test_mc_parse_tags_enchantment_excluded ─────────────────────────────
// data/<ns>/tags/enchantment/*.json (the STANDARD enchantment-tag
// location) must NOT be parsed as enchantment files.  A substring match on
// "/enchantment/" used to catch them, extracting ns="minecraft/tags" and
// producing a "Skipping (max_level=0, anvil_cost=0)" WARN per tag file.

TEST_CASE("test_mc_parse_tags_enchantment_excluded") {
    std::unordered_map<std::string, std::string> files;
    files["data/minecraft/tags/enchantment/treasure.json"] = R"({
        "values": ["minecraft:mending", "minecraft:frost_walker"]
    })";
    files["data/minecraft/enchantment/sharpness.json"] = R"({
        "anvil_cost": 1,
        "max_level": 5,
        "exclusive_set": [],
        "supported_items": ["#minecraft:sword"]
    })";

    auto result = McOfficialParser::parse_files(files);
    expect_eq(static_cast<int>(result.enchantments.size()), 1,
              "tags/enchantment/*.json is NOT parsed as an enchantment");
    expect_eq(result.enchantments[0].id, std::string("minecraft:sharpness"),
              "only the real enchantment path (data/<ns>/enchantment/) is parsed");
    TEST_PASS("test_mc_parse_tags_enchantment_excluded");
}

// ─── test_mc_parse_files_empty ─────────────────────────────────────────
// parse_files() with an empty file map. Verify empty results with no crash.

TEST_CASE("test_mc_parse_files_empty") {
    std::unordered_map<std::string, std::string> files;

    auto result = McOfficialParser::parse_files(files);
    const auto& enchantments = result.enchantments;
    const auto& equipment = result.equipment;

    expect(enchantments.empty(), "mc_files_empty: no enchantments");
    expect(equipment.empty(), "mc_files_empty: no equipment");

    TEST_PASS("test_mc_parse_files_empty");
}

// ─── test_mc_official_single_string_supported ──────────────────────────
// T10: real MC 1.21+ datapack format allows supported_items to be a SINGLE
// STRING (e.g. "#minecraft:swords") rather than an array. Regression: the
// array-only parser threw JsonException on a string. Fixture (copied from the
// git-ignored res/More Enchants 1.4/...): data/tests/datapack/attack_speed.json.
// Verify it does NOT throw and the raw tag reference survives in applicable_to.

TEST_CASE("test_mc_official_single_string_supported") {
    TagResolver tag_resolver;

    std::string content = file_utils::read_file("data/tests/datapack/attack_speed.json");

    auto ench = McOfficialParser::parse_single_enchantment("enchantments", "attack_speed", content, tag_resolver);

    expect_eq(ench.id, std::string("enchantments:attack_speed"), "mc_single_string: ench id");
    expect_eq(ench.multiplier, 7, "mc_single_string: anvil_cost -> multiplier");
    expect_eq(ench.max_level, 3, "mc_single_string: max_level");

    // supported_items was a single string "#minecraft:swords" — passed through RAW
    bool has_swords = false;
    for (const auto& a : ench.applicable_to) {
        if (a == "#minecraft:swords") {
            has_swords = true;
            break;
        }
    }
    expect(has_swords, "mc_single_string: applicable_to contains raw #minecraft:swords");

    TEST_PASS("test_mc_official_single_string_supported");
}

// ─── test_mc_official_array_supported_items ────────────────────────────
// T10: real MC datapack also uses supported_items as an ARRAY of concrete
// item IDs (no "#" prefix). Fixture (copied from the git-ignored
// res/enchantments-encore-4.6/...): data/tests/datapack/moonwalk.json.
// Verify the array form still parses and concrete IDs pass through unchanged.

TEST_CASE("test_mc_official_array_supported_items") {
    TagResolver tag_resolver;

    std::string content = file_utils::read_file("data/tests/datapack/moonwalk.json");

    auto ench = McOfficialParser::parse_single_enchantment("enchantencore", "moonwalk", content, tag_resolver);

    expect_eq(ench.id, std::string("enchantencore:moonwalk"), "mc_array_supp: ench id");
    expect_eq(ench.multiplier, 4, "mc_array_supp: anvil_cost -> multiplier");
    expect_eq(ench.max_level, 3, "mc_array_supp: max_level");

    bool has_elytra = false;
    for (const auto& a : ench.applicable_to) {
        if (a == "minecraft:elytra") {
            has_elytra = true;
            break;
        }
    }
    expect(has_elytra, "mc_array_supp: applicable_to contains minecraft:elytra");

    TEST_PASS("test_mc_official_array_supported_items");
}

// ─── test_mc_single_string_exclusive_set ───────────────────────────────
// T10: exclusive_set may also be a single string reference (robustness of the
// collect_strings helper). Empty TagResolver → tag resolves to nothing, but
// parsing must NOT throw.

TEST_CASE("test_mc_single_string_exclusive_set") {
    TagResolver tag_resolver;

    std::string content = R"({
        "anvil_cost": 1,
        "max_level": 5,
        "exclusive_set": "#minecraft:sword",
        "supported_items": "#minecraft:sword"
    })";

    auto ench = McOfficialParser::parse_single_enchantment("minecraft", "sharpness", content, tag_resolver);

    expect_eq(ench.id, std::string("minecraft:sharpness"), "mc_single_excl: ench id");
    expect(ench.exclusive_with.empty(), "mc_single_excl: unresolved tag resolves to empty exclusive_with");

    TEST_PASS("test_mc_single_string_exclusive_set");
}

// ─── test_mc_tag_replace_semantics ─────────────────────────────────────
// T10: MC 1.21 tag files may carry "replace": true → the definition REPLACES
// any existing tag with the same key instead of merging. Absent / false → the
// definitions MERGE (datapack semantics).

TEST_CASE("test_mc_tag_replace_semantics") {
    TagResolver resolver;

    // First definition for the tag
    resolver.load_tag_content("minecraft:swords", R"({"values": ["minecraft:stone_sword", "minecraft:wooden_sword"]})");

    // Without "replace", a second definition MERGES with the first
    resolver.load_tag_content("minecraft:swords", R"({"values": ["minecraft:iron_sword"]})");
    auto merged = resolver.resolve("#minecraft:swords");
    expect(merged.contains("minecraft:stone_sword"), "tag_replace: merge keeps first tag's values");
    expect(merged.contains("minecraft:iron_sword"), "tag_replace: merge adds second tag's values");

    // With "replace": true, the tag is replaced entirely
    resolver.load_tag_content("minecraft:swords", R"({"replace": true, "values": ["minecraft:diamond_sword"]})");
    auto replaced = resolver.resolve("#minecraft:swords");
    expect_eq(static_cast<int>(replaced.size()), 1, "tag_replace: replaced tag has only the new value");
    expect(replaced.contains("minecraft:diamond_sword"), "tag_replace: replaced tag contains the new value");

    TEST_PASS("test_mc_tag_replace_semantics");
}

// ─── test_mc_tag_object_entry ──────────────────────────────────────────
// B-T25: real MC 1.21+ tag files allow object entries
//   { "id": "minecraft:diamond_sword", "required": false }
// alongside plain strings.  The object's `id` must be preserved so the member
// is not silently dropped from item_tags / derived equipment / the resolver.

TEST_CASE("test_mc_tag_object_entry") {
    std::unordered_map<std::string, std::string> files;
    files["data/minecraft/tags/item/swords.json"] = R"({
        "values": [
            "minecraft:stone_sword",
            {"id": "minecraft:diamond_sword", "required": false}
        ]
    })";

    auto result = McOfficialParser::parse_files(files);

    // item_tags: the object entry id must survive into the tag definition
    expect_eq(static_cast<int>(result.item_tags.size()), 1, "mc_tag_object: one item tag definition");
    expect_eq(result.item_tags[0].key, std::string("minecraft:swords"), "mc_tag_object: tag key");
    bool has_diamond = false, has_stone = false;
    for (const auto& v : result.item_tags[0].values) {
        if (v == "minecraft:diamond_sword")
            has_diamond = true;
        if (v == "minecraft:stone_sword")
            has_stone = true;
    }
    expect(has_diamond, "mc_tag_object: object entry id present in item_tags values");
    expect(has_stone, "mc_tag_object: plain string entry preserved");

    // equipment: the object entry id contributes to derived equipment
    bool eq_diamond = false;
    for (const auto& e : result.equipment) {
        if (e.id == "minecraft:diamond_sword") {
            eq_diamond = true;
            break;
        }
    }
    expect(eq_diamond, "mc_tag_object: object entry id in derived equipment");

    // resolver: loading the item tag preserves the object entry member
    TagResolver resolver;
    McOfficialParser::load_item_tags_into(resolver, result.item_tags);
    auto resolved = resolver.resolve("#minecraft:swords");
    expect(resolved.contains("minecraft:diamond_sword"), "mc_tag_object: resolver resolves object entry id");
    expect(resolved.contains("minecraft:stone_sword"), "mc_tag_object: resolver keeps plain string entry");

    TEST_PASS("test_mc_tag_object_entry");
}

// ============================================================================
// Main
// ============================================================================
