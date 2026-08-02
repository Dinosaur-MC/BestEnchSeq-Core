#include "ds/ds.h"
#include "ds/Field.h"
#include "ds/codec/Codecs.h"
#include "ds/codec/Converter.h"
#include "ds/json/JsonBinder.h"
#include "common/CommonTypes.h"   // NSID（引擎不依赖，测试引入以证明可接入）
#include "framework/test_utils.h"
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <vector>

// ── 1. ErrorList 收集 + ValidationError 聚合 ──────────────────────────
void test_error_collection() {
    ds::ErrorList err;
    expect(err.empty(), "fresh ErrorList empty");
    err.add("a.b", "bad");
    err.add("c", "also bad");
    expect(!err.empty(), "errors collected");
    expect(err.size() == 2, "two errors");
    expect(err.errors()[0].path == "a.b", "path recorded");
    expect(err.errors()[0].message == "bad", "message recorded");
    TEST_PASS("ErrorList collects");
}
void test_validation_error_aggregates() {
    ds::ErrorList err;
    err.add("x", "e1");
    err.add("y", "e2");
    ds::ValidationError ve(std::move(err));
    std::string s = ve.what();
    expect(s.find("x") != std::string::npos && s.find("e1") != std::string::npos,
           "validation error aggregates path+message");
    expect(s.find("y") != std::string::npos && s.find("e2") != std::string::npos,
           "second error also aggregated");
    expect(ve.errors().size() == 2, "structured errors survive move into exception");
    expect(ve.errors().errors()[0].path == "x" && ve.errors().errors()[0].message == "e1",
           "first error retained");
    TEST_PASS("ValidationError aggregates");
}

// ── 2. Field 描述符 ──────────────────────────────────────────────────
struct Demo {
    std::string id;
    int level = 0;
};

// 占位 codec：Field 的 get/set/should_emit 测试不需要真实 codec 方法。
// 真实标量 codec（string_codec/int_codec）在 Task 3 引入。
struct DummyCodec {};

void test_field_descriptor() {
    auto f = ds::field("id", &Demo::id, DummyCodec{});
    Demo d{"abc", 5};
    expect(f.name == std::string("id"), "field name carried");
    expect(f.get(d) == "abc", "field get reads member");
    f.set(d, std::string("xyz"));
    expect(d.id == "xyz", "field set writes member");
    expect(f.should_emit(d), "default emit predicate always true");
    TEST_PASS("field descriptor get/set");
}
void test_field_emit_predicate() {
    auto f = ds::field("level", &Demo::level, DummyCodec{},
                       [](const Demo& o) { return o.level > 0; });
    Demo d{"a", 0};
    expect(!f.should_emit(d), "emit false when predicate false");
    d.level = 7;
    expect(f.should_emit(d), "emit true when predicate true");
    TEST_PASS("field custom emit predicate");
}
void test_field_aliases_and_required() {
    auto fa = ds::field("id", &Demo::id, DummyCodec{}, "old_id");
    expect(fa.aliases.size() == 1 && std::string(fa.aliases[0]) == "old_id",
           "single alias lands in aliases, not emit");
    auto fb = ds::field("id", &Demo::id, DummyCodec{}, "a", "b");
    expect(fb.aliases.size() == 2 && std::string(fb.aliases[1]) == "b", "multi alias");
    auto fr = ds::required_field("id", &Demo::id, DummyCodec{});
    expect(fr.required, "required_field sets required");
    expect(!fa.required, "plain field not required");
    TEST_PASS("field aliases + required");
}
// ── 3. 基础标量 codec 往返 + 校验 ──────────────────────────────────
void test_scalar_codecs() {
    // string
    Json j1; ds::string_codec{}.to_json(std::string("hi"), j1);
    std::string s1; ds::ErrorList e1;
    expect(ds::string_codec{}.from_json(j1, s1, e1, "s") && s1 == "hi",
           "string roundtrip");
    // int
    Json j2; ds::int_codec{}.to_json(42, j2);
    int i2 = 0; ds::ErrorList e2;
    expect(ds::int_codec{}.from_json(j2, i2, e2, "i") && i2 == 42, "int roundtrip");
    // float
    Json j3; ds::float_codec{}.to_json(1.5f, j3);
    float f3 = 0; ds::ErrorList e3;
    expect(ds::float_codec{}.from_json(j3, f3, e3, "f") && f3 == 1.5f, "float roundtrip");
    // bool
    Json j4; ds::bool_codec{}.to_json(true, j4);
    bool b4 = false; ds::ErrorList e4;
    expect(ds::bool_codec{}.from_json(j4, b4, e4, "b") && b4, "bool roundtrip");
    TEST_PASS("scalar codecs roundtrip");
}
void test_int_range_validation() {
    Json j; j = Json(int64_t{99});
    int v = 0; ds::ErrorList e;
    expect(!ds::int_codec{0, 10}.from_json(j, v, e, "level"),
           "int out of range rejected");
    expect(e.size() == 1 && e.errors()[0].path == "level", "range error path");
    TEST_PASS("int range validation");
}
void test_type_mismatch() {
    Json j; j = Json(std::string("x"));   // string where number expected
    int v = 0; ds::ErrorList e;
    expect(!ds::int_codec{}.from_json(j, v, e, "n"), "type mismatch rejected");
    TEST_PASS("type mismatch rejected");
}
void test_scalar_csv_roundtrip() {
    std::string c1; ds::string_codec{}.to_csv(std::string("hi"), c1);
    std::string s1; ds::ErrorList e1;
    expect(ds::string_codec{}.from_csv(c1, s1, e1, "s") && s1 == "hi", "string csv roundtrip");
    std::string c2; ds::int_codec{}.to_csv(42, c2);
    int i2 = 0; ds::ErrorList e2;
    expect(ds::int_codec{}.from_csv(c2, i2, e2, "i") && i2 == 42, "int csv roundtrip");
    std::string c3; ds::float_codec{}.to_csv(1.5f, c3);
    float f3 = 0; ds::ErrorList e3;
    expect(ds::float_codec{}.from_csv(c3, f3, e3, "f") && f3 == 1.5f, "float csv roundtrip");
    std::string c4; ds::bool_codec{}.to_csv(true, c4);
    bool b4 = false; ds::ErrorList e4;
    expect(ds::bool_codec{}.from_csv(c4, b4, e4, "b") && b4, "bool csv roundtrip");
    TEST_PASS("scalar csv roundtrip");
}
void test_csv_range_and_partial_rejected() {
    int v = 0; ds::ErrorList e;
    expect(!ds::int_codec{0, 10}.from_csv("500", v, e, "lv"), "csv range rejected");
    expect(e.size() == 1, "csv range error");
    int v2 = 0; ds::ErrorList e2;
    expect(!ds::int_codec{}.from_csv("42abc", v2, e2, "lv"), "csv partial parse rejected");
    TEST_PASS("csv range + partial rejected");
}
// ── 4. JSON 绑定 ────────────────────────────────────────────────────
struct Person {
    std::string name;
    int age = 0;
    bool active = false;
};
struct PersonSchema {
    using Type = Person;
    static constexpr auto fields = std::tuple{
        ds::required_field("name", &Person::name, ds::string_codec{}),
        ds::field("age",  &Person::age,  ds::int_codec{0, 150}),
        ds::field("active", &Person::active, ds::bool_codec{}),
    };
};
using PersonJson = ds::json::Schema<PersonSchema>;
static_assert(std::tuple_size_v<decltype(PersonSchema::fields)> == 3, "schema fields constexpr-evaluable");

void test_json_roundtrip() {
    Person p{"alice", 30, true};
    Json j = PersonJson::serialize(p);
    Person out;
    ds::ErrorList e;
    expect(PersonJson::parse(j, out, e), "parse ok");
    expect(out.name == "alice" && out.age == 30 && out.active, "roundtrip equal");
    TEST_PASS("json roundtrip");
}
void test_json_required_missing() {
    Json j = Json::object().set("age", Json(int64_t{5}));   // name missing
    Person out; ds::ErrorList e;
    expect(!PersonJson::parse(j, out, e), "required missing fails");
    expect(e.size() == 1 && e.errors()[0].path == "name", "missing required path");
    TEST_PASS("json required missing");
}
void test_json_unknown_key_tolerant_vs_strict() {
    Json j = Json::object()
        .set("name", Json(std::string("bob")))
        .set("age", Json(int64_t{1}))
        .set("extra", Json(int64_t{999}));
    Person out; ds::ErrorList e;
    expect(PersonJson::parse(j, out, e), "unknown key tolerated by default");
    expect(e.empty(), "no errors when tolerant");
    using StrictPersonJson = ds::json::Schema<PersonSchema, true>;
    Person out2; ds::ErrorList e2;
    expect(!StrictPersonJson::parse(j, out2, e2), "strict rejects unknown key");
    expect(e2.size() == 1 && e2.errors()[0].path == "extra", "unknown key path");
    TEST_PASS("json unknown key tolerant/strict");
}
void test_json_parse_or_throw() {
    Json j = Json::object().set("age", Json(int64_t{5}));
    Person out;
    bool threw = false;
    try { PersonJson::parse_or_throw(j, out); }
    catch (const ds::ValidationError& ve) { threw = true; expect(ve.errors().size() == 1, "aggregated"); }
    expect(threw, "parse_or_throw throws on errors");
    TEST_PASS("json parse_or_throw");
}
void test_json_collective_multi_error() {
    // name missing (required) AND age wrong type (string where int expected) AND active wrong type
    Json j = Json::object()
        .set("age", Json(std::string("oops")))
        .set("active", Json(int64_t{5}));
    Person out; ds::ErrorList e;
    expect(!PersonJson::parse(j, out, e), "multi-error parse fails");
    expect(e.size() == 3, "all three errors collected");   // name + age + active
    bool has_name = false, has_age = false, has_active = false;
    for (const auto& fe : e.errors()) {
        if (fe.path == "name") has_name = true;
        if (fe.path == "age") has_age = true;
        if (fe.path == "active") has_active = true;
    }
    expect(has_name && has_age && has_active, "errors carry distinct paths");
    TEST_PASS("json collective multi-error");
}

// ── 5. Converter 适配（引擎零领域依赖）────────────────────────────
struct NSIDConverter {                      // 用户定义，测试内建
    using value_type = NSID;
    static std::string to_string(const NSID& id) { return id.str(); }
    // NSID(s) 会抛异常（非法标识符）——text_codec 内部 try/catch 防御，无需在此兜底。
    static std::optional<NSID> from_string(std::string_view s) { return NSID(s); }
};
struct PlatformConv {                       // enum ↔ 字符串
    using value_type = MCE;
    static std::string to_string(MCE p) {
        switch (p) { case MCE::Java: return "java"; case MCE::Bedrock: return "bedrock";
                     case MCE::All: return "all"; default: return "none"; }
    }
    static std::optional<MCE> from_string(std::string_view s) {
        if (s == "java") return MCE::Java;
        if (s == "bedrock") return MCE::Bedrock;
        if (s == "all") return MCE::All;
        if (s == "none") return MCE::None;
        return std::nullopt;
    }
};

// json_codec：富类型 ↔ Json（CSV 无自然表示 → from_csv 报错、to_csv 抛异常）。
struct Point { int x = 0; int y = 0; };
struct PointConv {
    using value_type = Point;
    static Json to_json(const Point& p) {
        return Json::object().set("x", Json(int64_t{p.x})).set("y", Json(int64_t{p.y}));
    }
    static bool from_json(const Json& j, Point& p) {
        if (j.type() != JsonType::Object || !j.has("x") || !j.has("y")) return false;
        p.x = static_cast<int>(j["x"].as<int64_t>());
        p.y = static_cast<int>(j["y"].as<int64_t>());
        return true;
    }
};
struct Holder { Point pt; };
struct HolderSchema {
    using Type = Holder;
    static constexpr auto fields = std::tuple{
        ds::field("pt", &Holder::pt, ds::json_codec<PointConv>{}),
    };
};
using HolderJson = ds::json::Schema<HolderSchema>;

struct Equip {
    NSID id;
    MCE platform = MCE::All;
    int durability = 0;
};
struct EquipSchema {
    using Type = Equip;
    static constexpr auto fields = std::tuple{
        ds::required_field("id", &Equip::id, ds::text_codec<NSIDConverter>{}),
        ds::field("platform", &Equip::platform, ds::text_codec<PlatformConv>{}),
        ds::field("durability", &Equip::durability, ds::int_codec{}),
    };
};
using EquipJson = ds::json::Schema<EquipSchema>;

void test_text_codec_roundtrip() {
    Equip eq{NSID("minecraft:diamond_sword"), MCE::Java, 1561};
    Json j = EquipJson::serialize(eq);
    Equip out; ds::ErrorList e;
    expect(EquipJson::parse(j, out, e), "equip parse ok");
    expect(out.id == eq.id, "NSID roundtrip");
    expect(out.platform == MCE::Java, "enum roundtrip via converter");
    expect(out.durability == 1561, "int roundtrip");
    TEST_PASS("text_codec (NSID + enum) roundtrip");
}
void test_text_codec_invalid_rejected() {
    Json j = Json::object().set("id", Json(std::string("BadID!"))).set("durability", Json(int64_t{1}));
    Equip out; ds::ErrorList e;
    expect(!EquipJson::parse(j, out, e), "invalid NSID rejected");
    expect(e.size() == 1 && e.errors()[0].path == "id", "NSID invalid error path");
    TEST_PASS("text_codec invalid value rejected");
}
void test_json_codec() {
    Holder h{{3, 4}};
    Json j = HolderJson::serialize(h);
    Holder out; ds::ErrorList e;
    expect(HolderJson::parse(j, out, e), "json_codec roundtrip");
    expect(out.pt.x == 3 && out.pt.y == 4, "json_codec values");
    // from_csv rejects loudly
    Point p; ds::ErrorList e2;
    expect(!ds::json_codec<PointConv>{}.from_csv("3;4", p, e2, "pt"), "json_codec from_csv rejects");
    expect(e2.size() == 1, "from_csv error recorded");
    // to_csv throws
    std::string cell; bool threw = false;
    try { ds::json_codec<PointConv>{}.to_csv(h.pt, cell); } catch (const std::logic_error&) { threw = true; }
    expect(threw, "json_codec to_csv throws");
    TEST_PASS("json_codec roundtrip + CSV rejection");
}

// ── 6. 结构型 codec（vector/set/optional）────────────────────────────
struct Vals {
    std::vector<int> nums;
    std::unordered_set<std::string> tags;
    std::optional<int> maybe;
};
struct ValsSchema {
    using Type = Vals;
    static constexpr auto fields = std::tuple{
        ds::field("nums",  &Vals::nums,  ds::vector_codec<ds::int_codec>{}),
        ds::field("tags",  &Vals::tags,  ds::set_codec<ds::string_codec>{}),
        ds::field("maybe", &Vals::maybe, ds::optional_codec<ds::int_codec>{}),
    };
};
using ValsJson = ds::json::Schema<ValsSchema>;

void test_vector_codec() {
    Vals v{{1, 2, 3}, {}, std::nullopt};
    Json j = ValsJson::serialize(v);
    Vals out; ds::ErrorList e;
    expect(ValsJson::parse(j, out, e), "vector parse ok");
    expect(out.nums.size() == 3 && out.nums[0] == 1 && out.nums[2] == 3, "vector roundtrip");
    TEST_PASS("vector codec roundtrip");
}
void test_set_codec() {
    Vals v{{}, {"b", "a", "c"}, std::nullopt};
    Json j = ValsJson::serialize(v);
    Vals out; ds::ErrorList e;
    expect(ValsJson::parse(j, out, e), "set parse ok");
    expect(out.tags.count("a") && out.tags.count("b") && out.tags.count("c"),
           "set roundtrip (order-independent)");
    // deterministic sorted emission
    Json j2 = ValsJson::serialize(Vals{{}, {"b", "a", "c"}, std::nullopt});
    auto tags_json = j2.has("tags") ? j2["tags"] : Json();
    expect(tags_json.type() == JsonType::Array, "tags emitted as array");
    auto tag_arr = tags_json.as<Json::Array>();
    expect(tag_arr.size() == 3 && tag_arr[0].as<std::string>() == "a" &&
           tag_arr[1].as<std::string>() == "b" && tag_arr[2].as<std::string>() == "c",
           "set emitted deterministically sorted");
    TEST_PASS("set codec roundtrip");
}
void test_optional_codec() {
    Vals v{{}, {}, 42};
    Json j = ValsJson::serialize(v);
    Vals out; ds::ErrorList e;
    expect(ValsJson::parse(j, out, e), "optional parse ok");
    expect(out.maybe.has_value() && *out.maybe == 42, "optional present roundtrip");
    Vals v2{{}, {}, std::nullopt};
    Json j2 = ValsJson::serialize(v2);
    Vals out2; ds::ErrorList e2;
    expect(ValsJson::parse(j2, out2, e2), "optional absent parse ok");
    expect(!out2.maybe.has_value(), "optional absent roundtrip");
    TEST_PASS("optional codec present/absent");
}

// ── 7. 条件发射 + 别名 ──────────────────────────────────────────────
struct Cond {
    int limited_level = 0;
    bool provided = false;
    int min_cost_base = 0;
};
struct CondSchema {
    using Type = Cond;
    static constexpr auto fields = std::tuple{
        ds::field("limited_level", &Cond::limited_level, ds::int_codec{},
                  [](const Cond& c) { return c.provided; }),
        ds::field("min_cost_base", &Cond::min_cost_base, ds::int_codec{},
                  "min_cost.base"),                    // 别名：嵌套形态
    };
};
using CondJson = ds::json::Schema<CondSchema>;

void test_conditional_emit() {
    Cond c1{5, true, 0};
    Json j1 = CondJson::serialize(c1);
    expect(j1.has("limited_level"), "emitted when provided");
    Cond c2{5, false, 0};
    Json j2 = CondJson::serialize(c2);
    expect(!j2.has("limited_level"), "skipped when not provided");
    TEST_PASS("conditional emit predicate");
}
void test_alias_nested_form() {
    Json j = Json::object()
        .set("min_cost", Json::object().set("base", Json(int64_t{10})));
    Cond out; ds::ErrorList e;
    expect(CondJson::parse(j, out, e), "nested alias parse ok");
    expect(out.min_cost_base == 10, "alias sources value from nested path");
    TEST_PASS("alias nested path (min_cost.base)");
}

int main() {
    test_error_collection();
    test_validation_error_aggregates();
    test_field_descriptor();
    test_field_emit_predicate();
    test_field_aliases_and_required();
    test_scalar_codecs();
    test_int_range_validation();
    test_type_mismatch();
    test_scalar_csv_roundtrip();
    test_csv_range_and_partial_rejected();
    test_json_roundtrip();
    test_json_required_missing();
    test_json_unknown_key_tolerant_vs_strict();
    test_json_parse_or_throw();
    test_json_collective_multi_error();
    test_text_codec_roundtrip();
    test_text_codec_invalid_rejected();
    test_json_codec();
    test_vector_codec();
    test_set_codec();
    test_optional_codec();
    test_conditional_emit();
    test_alias_nested_form();
    return print_summary();
}
