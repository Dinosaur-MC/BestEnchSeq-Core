#include "ds/ds.h"
#include "domain/business/schemas/EnchInfoSchema.h"
#include "domain/business/schemas/EquipmentSchema.h"
#include "framework/test_utils.h"

#include <string>
#include <unordered_set>

// ── 领域/DTO schema 基础设施（Phase 2 迁移）────────────────────────────
// schema 与测试类型必须在命名空间作用域——局部类不能含 static constexpr 数据成员（C++20）。
// 别名引用 schema 头内定义的 binder 别名（本测试同时验证这些别名可用）。

using EnchJson      = business::schema::EnchJsonSchema;
using EnchCsv       = ds::csv::Schema<business::schema::EnchInfoSchema>;
using DataJson      = business::schema::EnchantmentDataJson;
using EquipJson     = business::schema::EquipJsonSchema;
using EquipTagJson  = business::schema::EquipTagJsonSchema;

EnchInfo make_ench() {
    EnchInfo e{NSID("minecraft:sharpness"), "Sharpness", MCE::Java, 5, 5, 1, true,
               {NSID("minecraft:smite")},
               {NSID("#minecraft:sword"), NSID("#minecraft:axe")},
               1, 11};
    e.limited_level_provided = true;
    return e;
}

void test_enchinfo_json_roundtrip() {
    EnchInfo e = make_ench();
    Json j = EnchJson::serialize(e);
    std::string s = j.to_string();
    expect(s.find("\"platform\":\"java\"") != std::string::npos, "platform key canonical");
    expect(s.find("supported_platform") == std::string::npos,
           "legacy supported_platform key not emitted (canonical = platform)");
    expect(s.find("limited_level") != std::string::npos, "limited_level emitted when provided");

    EnchInfo out; ds::ErrorList err;
    expect(EnchJson::parse(j, out, err), "ench json parse ok");
    expect(out.id == e.id, "id roundtrip");
    expect(out.name == "Sharpness", "name roundtrip");
    expect(out.supported_platform == MCE::Java, "platform roundtrip");
    expect(out.max_level == 5, "max_level roundtrip");
    expect(out.limited_level == 5, "limited_level value roundtrip");
    expect(out.limited_level_provided, "presence flag reconstructed on parse");
    expect(out.multiplier == 1, "multiplier roundtrip");
    expect(out.is_treasure, "is_treasure roundtrip");
    expect(out.exclusive_set == e.exclusive_set, "exclusive_set roundtrip");
    expect(out.supported_items == e.supported_items, "supported_items roundtrip");
    expect(out.min_cost_base == 1 && out.min_cost_per_level == 11, "min_cost roundtrip");

    // re-serialize after parse → limited_level still emitted (roundtrip stable)
    Json j2 = EnchJson::serialize(out);
    expect(j2.has("limited_level"), "re-serialize emits limited_level after presence reconstruction");
    expect(j2.has("min_cost_base") && j2.has("min_cost_per_level"), "min_cost stable on reserialize");
    TEST_PASS("EnchInfo JSON roundtrip");
}

void test_enchinfo_platform_legacy_alias() {
    Json j = Json::object()
        .set("id", Json(std::string("minecraft:sharpness")))
        .set("max_level", Json(int64_t{5}))
        .set("multiplier", Json(int64_t{1}))
        .set("supported_platform", Json(std::string("bedrock")));  // 旧键名，无 platform 键
    EnchInfo out; ds::ErrorList err;
    expect(EnchJson::parse(j, out, err), "legacy supported_platform alias accepted");
    expect(out.supported_platform == MCE::Bedrock, "legacy alias sourced to MCE::Bedrock");
    TEST_PASS("EnchInfo legacy platform alias (supported_platform)");
}

void test_enchinfo_min_cost_conditional() {
    EnchInfo e = make_ench();
    e.min_cost_base      = 0;
    e.min_cost_per_level = 0;
    Json j = EnchJson::serialize(e);
    expect(!j.has("min_cost_base"), "zero min_cost_base not emitted");
    expect(!j.has("min_cost_per_level"), "zero min_cost_per_level not emitted");

    EnchInfo e2 = make_ench();
    e2.min_cost_base      = 2;
    e2.min_cost_per_level = 4;
    Json j2 = EnchJson::serialize(e2);
    expect(j2.has("min_cost_base"), "nonzero min_cost_base emitted");
    expect(j2.has("min_cost_per_level"), "nonzero min_cost_per_level emitted");
    TEST_PASS("EnchInfo min_cost conditional emit");
}

void test_enchinfo_csv_roundtrip() {
    EnchInfo e = make_ench();
    auto hdr = EnchCsv::header();
    expect(hdr.size() == 11, "11 csv columns");
    expect(hdr[2] == "platform", "csv header third col is platform");
    auto row = EnchCsv::serialize_row(e);
    EnchInfo out; ds::ErrorList err;
    expect(EnchCsv::parse_row(hdr, row, out, err), "ench csv parse ok");
    expect(out.id == e.id, "csv id roundtrip");
    expect(out.supported_platform == MCE::Java, "csv platform roundtrip");
    expect(out.max_level == 5, "csv max_level roundtrip");
    expect(out.limited_level == 5, "csv limited_level roundtrip");
    expect(out.limited_level_provided, "csv presence flag reconstructed");
    expect(out.is_treasure, "csv is_treasure roundtrip");
    expect(out.exclusive_set == e.exclusive_set, "csv exclusive_set roundtrip");
    expect(out.supported_items == e.supported_items, "csv supported_items roundtrip");
    expect(out.min_cost_base == 1 && out.min_cost_per_level == 11, "csv min_cost roundtrip");
    TEST_PASS("EnchInfo CSV roundtrip");
}

void test_enchantment_data_json_min_cost_dual() {
    // 嵌套形态（MC 官方）
    Json nested = Json::object()
        .set("min_cost", Json::object()
            .set("base", Json(int64_t{5}))
            .set("per_level_above_first", Json(int64_t{9})));
    business::loader::EnchantmentData d1; ds::ErrorList e1;
    expect(DataJson::parse(nested, d1, e1), "nested min_cost form parses");
    expect(d1.min_cost_base == 5 && d1.min_cost_per_level == 9, "nested min_cost values extracted");
    // 扁平形态（native）
    Json flat = Json::object()
        .set("min_cost_base", Json(int64_t{7}))
        .set("min_cost_per_level", Json(int64_t{3}));
    business::loader::EnchantmentData d2; ds::ErrorList e2;
    expect(DataJson::parse(flat, d2, e2), "flat min_cost form parses");
    expect(d2.min_cost_base == 7 && d2.min_cost_per_level == 3, "flat min_cost values extracted");
    TEST_PASS("EnchantmentData min_cost dual-form (nested + flat)");
}

void test_enchantment_data_platform() {
    Json j = Json::object().set("platform", Json(std::string("java")));
    business::loader::EnchantmentData d1; ds::ErrorList e1;
    expect(DataJson::parse(j, d1, e1), "dto platform key parses");
    expect(d1.platform == "java", "dto canonical platform key sourced");
    Json j2 = Json::object().set("supported_platform", Json(std::string("java")));
    business::loader::EnchantmentData d2; ds::ErrorList e2;
    expect(DataJson::parse(j2, d2, e2), "dto legacy supported_platform alias parses");
    expect(d2.platform == "java", "dto legacy alias sourced");
    TEST_PASS("EnchantmentData platform canonical + alias");
}

void test_equipment_tag_roundtrip() {
    EquipmentTag t{NSID("#minecraft:sword"), "sword"};
    Json j = EquipTagJson::serialize(t);
    EquipmentTag out; ds::ErrorList err;
    expect(EquipTagJson::parse(j, out, err), "tag parse ok");
    expect(out.id == t.id, "tag id roundtrip");
    expect(out.name == "sword", "tag name roundtrip");
    TEST_PASS("EquipmentTag JSON roundtrip");
}

void test_equipment_schema_roundtrip() {
    Equipment eq{NSID("minecraft:diamond_sword"), "Diamond Sword", NSID("#minecraft:sword"), 1561};
    Json j = EquipJson::serialize(eq);
    Equipment out; ds::ErrorList err;
    expect(EquipJson::parse(j, out, err), "equipment parse ok");
    expect(out.id == eq.id, "equipment id roundtrip");
    expect(out.name == "Diamond Sword", "equipment name roundtrip");
    expect(out.category == eq.category, "equipment category roundtrip");
    expect(out.max_durability == 1561, "equipment max_durability roundtrip");
    TEST_PASS("Equipment JSON roundtrip");
}

int main() {
    test_enchinfo_json_roundtrip();
    test_enchinfo_platform_legacy_alias();
    test_enchinfo_min_cost_conditional();
    test_enchinfo_csv_roundtrip();
    test_enchantment_data_json_min_cost_dual();
    test_enchantment_data_platform();
    test_equipment_tag_roundtrip();
    test_equipment_schema_roundtrip();
    return print_summary();
}
