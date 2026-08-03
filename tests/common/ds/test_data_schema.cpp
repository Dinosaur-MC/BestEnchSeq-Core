#include "ds/ds.h"
#include "ds/Field.h"
#include "ds/codec/Codecs.h"
#include "ds/codec/Converter.h"
#include "ds/json/JsonBinder.h"
#include "ds/csv/CsvBinder.h"
#include "common/CommonTypes.h"   // NSID（引擎不依赖，测试引入以证明可接入）
#include "framework/test_utils.h"
#include <optional>
#include <sstream>
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
void test_int_narrow_type_guard() {
    // int32_t 目标收到超出 int32 范围的值 → 拒绝，而非静默截断（review M2）
    Json j; j = Json(int64_t{3000000000LL});
    int32_t v = 0; ds::ErrorList e;
    expect(!ds::int_codec{}.from_json(j, v, e, "n"), "value beyond int32 rejected");
    expect(e.size() == 1 && e.errors()[0].path == "n", "narrow error path");
    // CSV 侧同样拒绝
    int32_t v2 = 0; ds::ErrorList e2;
    expect(!ds::int_codec{}.from_csv("3000000000", v2, e2, "n"), "csv beyond int32 rejected");
    // 正常值仍通过
    Json j2; j2 = Json(int64_t{42});
    int32_t ok = 0; ds::ErrorList e3;
    expect(ds::int_codec{}.from_json(j2, ok, e3, "n") && ok == 42, "in-range int32 still ok");
    TEST_PASS("int_codec narrow-type guard");
}
void test_int_double_bounds_no_ub() {
    // 超大 double（1e300，超出 int64）→ cast 前拦截（防 [conv.fpint] UB，review M1）
    Json j = Json::parse("{\"x\": 1e300}");
    int v = 0; ds::ErrorList e;
    expect(!ds::int_codec{}.from_json(j["x"], v, e, "x"), "1e300 rejected");
    expect(e.size() == 1 && e.errors()[0].path == "x", "huge double error path");
    // 2^63（> INT64_MAX）拒绝
    Json j2 = Json::parse("{\"x\": 9223372036854775808.0}");
    int v2 = 0; ds::ErrorList e2;
    expect(!ds::int_codec{}.from_json(j2["x"], v2, e2, "x"), "2^63 rejected");
    // -2^63 与 < 2^63 的最大可表示 double，用 int64_t 目标精确解析
    Json j3 = Json::parse("{\"x\": -9223372036854775808.0}");
    int64_t v3 = 0; ds::ErrorList e3;
    expect(ds::int_codec{}.from_json(j3["x"], v3, e3, "x") && v3 == INT64_MIN,
           "-2^63 representable in int64_t");
    Json j4 = Json::parse("{\"x\": 9223372036854774784.0}");   // 2^63 - 1024
    int64_t v4 = 0; ds::ErrorList e4;
    expect(ds::int_codec{}.from_json(j4["x"], v4, e4, "x") && v4 == 9223372036854774784LL,
           "largest double < 2^63 parses");
    // 小数 double 仍报 expected integer
    Json j5 = Json::parse("{\"x\": 3.5}");
    int v5 = 0; ds::ErrorList e5;
    expect(!ds::int_codec{}.from_json(j5["x"], v5, e5, "x"), "fractional double rejected");
    TEST_PASS("int_codec double bounds no UB");
}
void test_csv_trailing_separator_no_spurious() {
    // 尾随 ';' 不再产出多余空元素（review L1）
    std::vector<std::string> v;
    ds::ErrorList e;
    ds::vector_codec<ds::string_codec>{}.from_csv("a;b;", v, e, "v");
    expect(v.size() == 2 && v[0] == "a" && v[1] == "b", "trailing sep drops last empty");
    expect(e.empty(), "no errors");
    // 中间空段保留（空串元素可往返）
    std::vector<std::string> v2;
    ds::ErrorList e2;
    ds::vector_codec<ds::string_codec>{}.from_csv("a;;b", v2, e2, "v");
    expect(v2.size() == 3 && v2[1].empty(), "middle empty segment kept");
    // set 侧同样
    std::unordered_set<std::string> s;
    ds::ErrorList e3;
    ds::set_codec<ds::string_codec>{}.from_csv("x;y;", s, e3, "s");
    expect(s.size() == 2 && s.count("x") == 1 && s.count("y") == 1,
           "set trailing sep no empty element");
    TEST_PASS("csv trailing separator no spurious element");
}
void test_float_csv_roundtrip_exact() {
    // float_codec::to_csv 输出可被 from_csv 精确往返（to_chars shortest-roundtrip 保证）
    const double vals[] = {0.0, 1.0, 3.141592653589793, 1e-300, 1.7976931348623157e308};
    bool all_ok = true;
    for (double d : vals) {
        std::string c;
        ds::float_codec{}.to_csv(d, c);
        double back = 0;
        ds::ErrorList e;
        if (!ds::float_codec{}.from_csv(c, back, e, "f") || back != d) all_ok = false;
    }
    expect(all_ok, "float csv to_chars roundtrip exact");
    TEST_PASS("float csv exact roundtrip");
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
using PersonCsv = ds::csv::Schema<PersonSchema>;
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

// ── 8. CSV 绑定 ────────────────────────────────────────────────────
void test_csv_header_and_roundtrip() {
    auto hdr = PersonCsv::header();
    expect(hdr.size() == 3 && hdr[0] == "name" && hdr[1] == "age", "header order");
    Person p{"alice", 30, true};
    auto row = PersonCsv::serialize_row(p);
    expect(row.size() == 3 && row[0] == "alice" && row[1] == "30" && row[2] == "true",
           "row serialized");
    Person out; ds::ErrorList e;
    expect(PersonCsv::parse_row(hdr, row, out, e), "row parse ok");
    expect(out.name == "alice" && out.age == 30 && out.active, "csv roundtrip");
    TEST_PASS("csv header + row roundtrip");
}
void test_csv_column_missing_optional() {
    // 缺 age 列（可选）→ 保持默认；缺 name 列（必填）→ 报错
    auto hdr = PersonCsv::header();
    Person out; ds::ErrorList e;
    expect(PersonCsv::parse_row({"name"}, {"bob"}, out, e), "missing optional col tolerated");
    expect(e.empty(), "no errors");
    Person out2; ds::ErrorList e2;
    expect(!PersonCsv::parse_row({"age"}, {"5"}, out2, e2), "missing required col fails");
    expect(e2.size() == 1 && e2.errors()[0].path == "name", "missing required col path");
    TEST_PASS("csv missing optional vs required column");
}
void test_csv_set_join_and_quotes() {
    Vals v{{}, {"x", "a"}, std::nullopt};
    using ValsCsv = ds::csv::Schema<ValsSchema>;
    auto row = ValsCsv::serialize_row(v);
    expect(row[1] == "a;x", "set ;-joined and sorted");
    Vals out; ds::ErrorList e;
    expect(ValsCsv::parse_row(ValsCsv::header(), row, out, e), "set row parse ok");
    expect(out.tags.count("a") && out.tags.count("x"), "set csv roundtrip");
    // 含逗号的值 → format_row 加引号
    auto quoted = csv::format_row({"a,b"});
    expect(quoted.find('"') != std::string::npos, "csv quoting for comma value");
    TEST_PASS("csv set join + quoting");
}

// 全文本层往返（format_row → split_line）所需：label 必填，验证空串可往返。
struct Note { std::string label; int n = 0; };
struct NoteSchema {
    using Type = Note;
    static constexpr auto fields = std::tuple{
        ds::required_field("label", &Note::label, ds::string_codec{}),
        ds::field("n", &Note::n, ds::int_codec{}),
    };
};
using NoteCsv = ds::csv::Schema<NoteSchema>;

void test_csv_full_text_roundtrip() {
    Person p{"alice, bob", 30, true};
    auto row = PersonCsv::serialize_row(p);
    auto table = ::csv::CsvTable{PersonCsv::header(), row};
    auto text = csv::format(table);
    // split back into lines
    std::vector<::csv::CsvRow> parsed;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        parsed.push_back(csv::split_line(line));
    }
    expect(parsed.size() == 2, "header + one data row");
    Person out; ds::ErrorList e;
    expect(PersonCsv::parse_row(parsed[0], parsed[1], out, e), "full text roundtrip parse");
    expect(out.name == "alice, bob" && out.age == 30 && out.active, "full text roundtrip values");
    TEST_PASS("csv full text roundtrip");
}
void test_csv_required_empty_string_roundtrip() {
    Note note{std::string{}, 7};   // required label is empty
    auto row = NoteCsv::serialize_row(note);
    expect(row[0].empty(), "empty label cell");
    Note out; ds::ErrorList e;
    expect(NoteCsv::parse_row(NoteCsv::header(), row, out, e), "empty required string roundtrips");
    expect(out.label.empty() && out.n == 7, "empty label + int retained");
    TEST_PASS("csv required empty-string roundtrip");
}

// ── 9. 验收：min_cost 双形态 + EnchInfoLike 端到端（spec §6.5/6.6） ──

// ── 9a. 双形态：嵌套 min_cost 对象 → 两个扁平字段（经别名） ────────
struct CostHolder { int min_cost_base = 0; int min_cost_per_level = 0; };
struct CostSchema {
    using Type = CostHolder;
    static constexpr auto fields = std::tuple{
        ds::field("min_cost_base", &CostHolder::min_cost_base, ds::int_codec{}, "min_cost.base"),
        ds::field("min_cost_per_level", &CostHolder::min_cost_per_level, ds::int_codec{}, "min_cost.per_level_above_first"),
    };
};
using CostJson = ds::json::Schema<CostSchema>;

void test_min_cost_dual_form() {
    // 嵌套形态（MC 官方）
    Json nested = Json::object()
        .set("min_cost", Json::object()
            .set("base", Json(int64_t{10}))
            .set("per_level_above_first", Json(int64_t{5})));
    CostHolder c; ds::ErrorList e;
    expect(CostJson::parse(nested, c, e), "nested form parses");
    expect(c.min_cost_base == 10 && c.min_cost_per_level == 5, "nested values extracted");
    // 扁平形态（native）
    Json flat = Json::object()
        .set("min_cost_base", Json(int64_t{7}))
        .set("min_cost_per_level", Json(int64_t{3}));
    CostHolder c2; ds::ErrorList e2;
    expect(CostJson::parse(flat, c2, e2), "flat form parses");
    expect(c2.min_cost_base == 7 && c2.min_cost_per_level == 3, "flat values extracted");
    // 序列化 → 扁平（canonical）
    CostHolder c3{2, 4};
    Json j = CostJson::serialize(c3);
    expect(j.has("min_cost_base") && !j.has("min_cost"), "serialize emits flat canonical");
    TEST_PASS("min_cost dual-form (nested + flat)");
}

// ── 9b. EnchInfo-式完整端到端（JSON + CSV 双往返） ──────────────────
struct EnchInfoLike {
    std::string id;
    std::string name;
    std::string platform;
    int max_level = 0;
    int multiplier = 0;
    bool is_treasure = false;
    int limited_level = 0;
    bool limited_level_provided = false;
    int min_cost_base = 0;
    int min_cost_per_level = 0;
    std::unordered_set<std::string> exclusive_set;
    std::unordered_set<std::string> supported_items;
};
struct EnchInfoLikeSchema {
    using Type = EnchInfoLike;
    static constexpr auto fields = std::tuple{
        ds::required_field("id", &Type::id, ds::string_codec{}),
        ds::field("name", &Type::name, ds::string_codec{}),
        ds::field("platform", &Type::platform, ds::string_codec{}, "supported_platform"),
        ds::field("max_level", &Type::max_level, ds::int_codec{.min = 1}),
        ds::field("multiplier", &Type::multiplier, ds::int_codec{.min = 1}),
        ds::field("is_treasure", &Type::is_treasure, ds::bool_codec{}),
        ds::field("limited_level", &Type::limited_level, ds::int_codec{},
                  [](const Type& t) { return t.limited_level_provided; },
                  ds::presence_flag<&Type::limited_level_provided>{}),
        ds::field("min_cost_base", &Type::min_cost_base, ds::int_codec{}, "min_cost.base"),
        ds::field("min_cost_per_level", &Type::min_cost_per_level, ds::int_codec{}, "min_cost.per_level_above_first"),
        ds::field("exclusive_set", &Type::exclusive_set, ds::set_codec<ds::string_codec>{}),
        ds::field("supported_items", &Type::supported_items, ds::set_codec<ds::string_codec>{}),
    };
};
using EnchJson = ds::json::Schema<EnchInfoLikeSchema>;
using EnchCsv  = ds::csv::Schema<EnchInfoLikeSchema>;

EnchInfoLike make_ench() {
    EnchInfoLike e;
    e.id = "minecraft:sharpness"; e.name = "Sharpness"; e.platform = "java";
    e.max_level = 5; e.multiplier = 1; e.is_treasure = true;
    e.limited_level = 5; e.limited_level_provided = true;
    e.min_cost_base = 1; e.min_cost_per_level = 11;
    e.exclusive_set = {"minecraft:smite"};
    e.supported_items = {"#minecraft:sword", "#minecraft:axe"};
    return e;
}

void test_enchlike_json_roundtrip() {
    EnchInfoLike e = make_ench();
    Json j = EnchJson::serialize(e);
    std::string s = j.to_string();
    expect(s.find("\"platform\":\"java\"") != std::string::npos, "platform key canonical");
    expect(s.find("limited_level") != std::string::npos, "conditional emitted when provided");
    EnchInfoLike out; ds::ErrorList err;
    expect(EnchJson::parse(j, out, err), "ench json parse ok");
    expect(out.id == e.id && out.platform == e.platform && out.max_level == 5, "basic fields");
    expect(out.exclusive_set == e.exclusive_set, "exclusive_set roundtrip");
    expect(out.supported_items == e.supported_items, "supported_items roundtrip");
    expect(out.min_cost_base == 1 && out.min_cost_per_level == 11, "min_cost roundtrip");
    expect(out.limited_level == 5, "limited_level value json roundtrip");
    expect(out.is_treasure, "is_treasure roundtrip");
    expect(out.limited_level_provided, "presence flag reconstructed on json parse");
    // re-serialize after parse → limited_level still emitted (roundtrip stable)
    Json j2 = EnchJson::serialize(out);
    expect(j2.has("limited_level"), "re-serialize emits limited_level after presence reconstruction");
    TEST_PASS("EnchInfoLike JSON roundtrip");
}

void test_presence_flag_absent() {
    Json j = Json::object()
        .set("id", Json(std::string("minecraft:sharpness")))
        .set("max_level", Json(int64_t{5}))
        .set("multiplier", Json(int64_t{1}));
    EnchInfoLike out; ds::ErrorList err;
    expect(EnchJson::parse(j, out, err), "parse without limited_level ok");
    expect(!out.limited_level_provided, "presence flag false when key absent");
    TEST_PASS("presence flag false when field absent");
}

void test_presence_flag_cleared_on_failed_parse() {
    EnchInfoLike o;
    ds::ErrorList err;
    Json okj = Json::object()
        .set("id", Json(std::string("minecraft:sharpness")))
        .set("max_level", Json(int64_t{5}))
        .set("multiplier", Json(int64_t{1}))
        .set("limited_level", Json(int64_t{5}));
    expect(EnchJson::parse(okj, o, err), "valid parse");
    expect(o.limited_level_provided, "flag true after valid parse");
    Json bad = Json::object()
        .set("id", Json(std::string("minecraft:sharpness")))
        .set("max_level", Json(int64_t{5}))
        .set("multiplier", Json(int64_t{1}))
        .set("limited_level", Json(std::string("not-a-number")));  // codec fails
    ds::ErrorList err2;
    expect(!EnchJson::parse(bad, o, err2), "invalid parse fails");
    expect(!o.limited_level_provided, "stale flag cleared on codec failure");
    TEST_PASS("presence flag cleared on failed parse");
}
void test_enchlike_platform_legacy_alias() {
    Json j = Json::object()
        .set("id", Json(std::string("minecraft:sharpness")))
        .set("max_level", Json(int64_t{5}))
        .set("multiplier", Json(int64_t{1}))
        .set("supported_platform", Json(std::string("bedrock")));  // 旧键名
    EnchInfoLike out; ds::ErrorList err;
    expect(EnchJson::parse(j, out, err), "legacy supported_platform alias accepted");
    expect(out.platform == "bedrock", "legacy alias sourced");
    TEST_PASS("legacy platform alias (supported_platform)");
}
void test_enchlike_csv_roundtrip() {
    EnchInfoLike e = make_ench();
    auto hdr = EnchCsv::header();
    expect(hdr[2] == "platform", "csv header third col is platform");
    auto row = EnchCsv::serialize_row(e);
    EnchInfoLike out; ds::ErrorList err;
    expect(EnchCsv::parse_row(hdr, row, out, err), "ench csv parse ok");
    expect(out.id == e.id && out.platform == "java" && out.max_level == 5, "csv basic fields");
    expect(out.supported_items == e.supported_items, "csv supported_items roundtrip");
    expect(out.exclusive_set == e.exclusive_set, "csv exclusive_set roundtrip");
    expect(out.limited_level == 5, "csv limited_level roundtrip");
    expect(out.is_treasure, "is_treasure roundtrip");
    expect(out.limited_level_provided, "csv presence flag reconstructed");
    TEST_PASS("EnchInfoLike CSV roundtrip");
}

void test_csv_presence_zero_counts_as_present() {
    EnchInfoLike e = make_ench();
    e.limited_level_provided = false;   // limited_level stays 0
    auto row = EnchCsv::serialize_row(e);
    EnchInfoLike out; ds::ErrorList err;
    expect(EnchCsv::parse_row(EnchCsv::header(), row, out, err), "csv parse ok");
    // CSV structurally cannot express "int field absent": cell "0" counts as present.
    expect(out.limited_level_provided, "csv non-empty cell counts as present (documented CSV limitation)");
    TEST_PASS("csv presence zero cell counts as present");
}

// ── 10. set_codec<int> 数值往返（非字符串内层，Task 6 review fold-in） ──
// 注：schema 与类型必须在命名空间作用域——局部类不能含 static constexpr 数据成员（C++20）。
struct IntSet { std::unordered_set<int> values; };
struct IntSetSchema {
    using Type = IntSet;
    static constexpr auto fields = std::tuple{
        ds::field("values", &IntSet::values, ds::set_codec<ds::int_codec>{}),
    };
};
using IntSetJson = ds::json::Schema<IntSetSchema>;

void test_set_codec_int_roundtrip() {
    IntSet s{{5, 1, 3}};
    Json j = IntSetJson::serialize(s);
    // values emitted as JSON numbers, sorted
    auto arr = j["values"].as<Json::Array>();
    expect(arr.size() == 3, "int set size");
    expect(arr[0].as<int64_t>() == 1 && arr[1].as<int64_t>() == 3 && arr[2].as<int64_t>() == 5,
           "int set emitted sorted numeric");
    IntSet out; ds::ErrorList e;
    expect(IntSetJson::parse(j, out, e), "int set parse ok");
    expect(out.values.count(1) && out.values.count(3) && out.values.count(5), "int set roundtrip");
    TEST_PASS("set_codec<int> numeric roundtrip");
}

// ── 11. Task 8 验收 review fold-ins ──────────────────────────────────────
// 11a. set_codec<text_codec<NSIDConverter>>：真实 EnchInfo 的 exclusive_set /
//      supported_items 是 unordered_set<NSID>，需该组合——此前从未组合往返验证。
struct NsidSet { std::unordered_set<NSID> ids; };
struct NsidSetSchema {
    using Type = NsidSet;
    static constexpr auto fields = std::tuple{
        ds::field("ids", &NsidSet::ids, ds::set_codec<ds::text_codec<NSIDConverter>>{}),
    };
};
using NsidSetJson = ds::json::Schema<NsidSetSchema>;
using NsidSetCsv  = ds::csv::Schema<NsidSetSchema>;

void test_nsid_set_roundtrip() {
    NsidSet s{ {NSID("minecraft:sharpness"), NSID("minecraft:knockback")} };
    // JSON
    Json j = NsidSetJson::serialize(s);
    NsidSet out; ds::ErrorList e;
    expect(NsidSetJson::parse(j, out, e), "nsid set json parse");
    expect(out.ids.count(NSID("minecraft:sharpness")) && out.ids.count(NSID("minecraft:knockback")),
           "nsid set json roundtrip");
    // CSV (;-joined, sorted)
    auto row = NsidSetCsv::serialize_row(s);
    NsidSet out2; ds::ErrorList e2;
    expect(NsidSetCsv::parse_row(NsidSetCsv::header(), row, out2, e2), "nsid set csv parse");
    expect(out2.ids.count(NSID("minecraft:knockback")) && out2.ids.count(NSID("minecraft:sharpness")),
           "nsid set csv roundtrip");
    TEST_PASS("set_codec<text_codec<NSIDConverter>> json+csv roundtrip");
}

// 11b. conditional emit + value roundtrip：本 schema 未声明 presence_flag，
//      故 limited_level_provided 不重建（emit 谓词只控制序列化；字段 codec 只拿到
//      本字段值，无法触达 provided）。重建需 presence_flag 机制（Task 10 已实现，
//      见 §9b EnchInfoLikeSchema 的 limited_level 字段）。本测试固定：无 presence_flag
//      时值往返 + 发射谓词生效。
struct HintCarrier { int limited_level = 0; bool provided = false; };
struct HintSchema {
    using Type = HintCarrier;
    static constexpr auto fields = std::tuple{
        ds::field("limited_level", &HintCarrier::limited_level, ds::int_codec{},
                  [](const Type& t) { return t.provided; }),
    };
};
using HintJson = ds::json::Schema<HintSchema>;

void test_custom_codec_value_roundtrip_with_conditional_emit() {
    // The value roundtrips and the emit predicate gates serialization. The
    // presence-derived flag (limited_level_provided) is NOT reconstructed by
    // this schema: it declares no presence_flag, so the binder never touches
    // `provided`. Reconstructing the flag requires declaring
    // ds::presence_flag<&...> on the field (implemented in Task 10; see the
    // limited_level field of EnchInfoLikeSchema in §9b).
    HintCarrier h{5, true};
    Json j = HintJson::serialize(h);
    expect(j.has("limited_level"), "emitted when provided");
    HintCarrier out;
    ds::ErrorList e;
    expect(HintJson::parse(j, out, e), "parse ok");
    expect(out.limited_level == 5, "value roundtrips");
    // NOTE: out.provided stays false (default) because HintSchema declares no
    // presence_flag on this field — the binder only writes a presence flag when
    // the schema opts in (see EnchInfoLikeSchema's limited_level in §9b).
    TEST_PASS("custom codec value roundtrip with conditional emit (flag not reconstructed)");
}

// ── 12. Task 10: schema 级 validate 钩子（spec §5 跨字段校验） ──────────
struct Invariant { int max_level = 0; int multiplier = 0; };
struct InvariantSchema {
    using Type = Invariant;
    static constexpr auto fields = std::tuple{
        ds::field("max_level", &Invariant::max_level, ds::int_codec{}),
        ds::field("multiplier", &Invariant::multiplier, ds::int_codec{}),
    };
    static void validate(Type& o, ds::ErrorList& err) {
        if (o.max_level < o.multiplier)
            err.add("max_level", "max_level must be >= multiplier");
    }
};
using InvariantJson = ds::json::Schema<InvariantSchema>;

void test_validate_hook() {
    Json okj = Json::object()
        .set("max_level", Json(int64_t{5})).set("multiplier", Json(int64_t{2}));
    Invariant ok; ds::ErrorList eok;
    expect(InvariantJson::parse(okj, ok, eok), "valid cross-field parse ok");
    expect(eok.empty(), "no errors when invariant holds");
    Json bad = Json::object()
        .set("max_level", Json(int64_t{1})).set("multiplier", Json(int64_t{5}));
    Invariant bado; ds::ErrorList eb;
    expect(!InvariantJson::parse(bad, bado, eb), "invariant violation fails parse");
    expect(eb.size() == 1 && eb.errors()[0].path == "max_level", "validate error path");
    TEST_PASS("schema validate hook cross-field");
}

void test_validate_hook_csv() {
    using InvariantCsv = ds::csv::Schema<InvariantSchema>;
    auto row = InvariantCsv::serialize_row(Invariant{1, 5});   // violates invariant
    Invariant out; ds::ErrorList err;
    expect(!InvariantCsv::parse_row(InvariantCsv::header(), row, out, err),
           "csv validate hook catches invariant violation");
    TEST_PASS("csv schema validate hook");
}

// ── 13. object_codec 嵌套对象（P0：inventory 嵌套 target / items 数组） ──
struct Addr {
    std::string street;
    std::string city;
};
struct AddrSchema {
    using Type = Addr;
    static constexpr auto fields = std::tuple{
        ds::required_field("street", &Addr::street, ds::string_codec{}),
        ds::field("city", &Addr::city, ds::string_codec{}),
    };
};
using AddrJson = ds::json::Schema<AddrSchema>;

struct Employee {
    std::string name;
    Addr home;                       // 嵌套对象字段
};
struct EmployeeSchema {
    using Type = Employee;
    static constexpr auto fields = std::tuple{
        ds::required_field("name", &Employee::name, ds::string_codec{}),
        ds::required_field("home", &Employee::home, ds::object_codec<AddrSchema>{}),
    };
};
using EmployeeJson = ds::json::Schema<EmployeeSchema>;

void test_object_codec_roundtrip() {
    Employee e{"alice", {"1 Main St", "Springfield"}};
    Json j = EmployeeJson::serialize(e);
    expect(j.has("home") && j["home"].type() == JsonType::Object, "nested field serialized as object");
    expect(j["home"]["street"].as<std::string>() == "1 Main St", "nested street serialized");
    expect(j["home"]["city"].as<std::string>() == "Springfield", "nested city serialized");
    Employee out; ds::ErrorList err;
    expect(EmployeeJson::parse(j, out, err), "nested object parse ok");
    expect(out.name == "alice" && out.home.street == "1 Main St" && out.home.city == "Springfield",
           "nested object roundtrip equal");
    TEST_PASS("object_codec nested object roundtrip");
}

// 对象数组：vector_codec<object_codec<X>> → items: [{...}, {...}]
struct Job {
    std::string title;
    int years = 0;
};
struct JobSchema {
    using Type = Job;
    static constexpr auto fields = std::tuple{
        ds::required_field("title", &Job::title, ds::string_codec{}),
        ds::field("years", &Job::years, ds::int_codec{}),
    };
};
struct Career {
    std::string person;
    std::vector<Job> jobs;
};
struct CareerSchema {
    using Type = Career;
    static constexpr auto fields = std::tuple{
        ds::required_field("person", &Career::person, ds::string_codec{}),
        ds::field("jobs", &Career::jobs, ds::vector_codec<ds::object_codec<JobSchema>>{}),
    };
};
using CareerJson = ds::json::Schema<CareerSchema>;

void test_object_codec_array_roundtrip() {
    Career c{"bob", {{"dev", 3}, {"lead", 5}}};
    Json j = CareerJson::serialize(c);
    auto arr = j["jobs"].as<Json::Array>();
    expect(arr.size() == 2, "array of objects serialized");
    expect(arr[0]["title"].as<std::string>() == "dev" && arr[0]["years"].as<int64_t>() == 3,
           "first element serialized");
    expect(arr[1]["title"].as<std::string>() == "lead" && arr[1]["years"].as<int64_t>() == 5,
           "second element serialized");
    Career out; ds::ErrorList err;
    expect(CareerJson::parse(j, out, err), "array of objects parse ok");
    expect(out.jobs.size() == 2 && out.jobs[0].title == "dev" && out.jobs[0].years == 3 &&
           out.jobs[1].title == "lead" && out.jobs[1].years == 5,
           "array of objects roundtrip equal");
    TEST_PASS("vector_codec<object_codec> array roundtrip");
}

void test_object_codec_required_nested_missing() {
    // 外层必填嵌套字段整体缺省 → 错误路径为外层字段名 "home"
    Json j = Json::object().set("name", Json(std::string("alice")));
    Employee out; ds::ErrorList err;
    expect(!EmployeeJson::parse(j, out, err), "missing required nested field fails");
    expect(err.size() == 1 && err.errors()[0].path == "home", "missing nested path is home");
    // 嵌套对象在场但内部必填子字段缺省 → 嵌套 parse 失败；内层错误路径带外层前缀
    Json j2 = Json::object()
        .set("name", Json(std::string("alice")))
        .set("home", Json::object().set("city", Json(std::string("Springfield"))));  // street 缺省
    Employee out2; ds::ErrorList err2;
    expect(!EmployeeJson::parse(j2, out2, err2), "missing inner required field fails");
    bool has_street = false;
    for (const auto& fe : err2.errors()) if (fe.path == "home.street") has_street = true;
    expect(has_street, "inner required error recorded (prefixed path)");
    TEST_PASS("object_codec required nested field missing");
}

void test_object_codec_wrong_type() {
    Json j = Json::object()
        .set("name", Json(std::string("alice")))
        .set("home", Json(std::string("not-an-object")));   // 字符串而非对象
    Employee out; ds::ErrorList err;
    expect(!EmployeeJson::parse(j, out, err), "nested field with wrong type fails");
    expect(err.size() == 1 && err.errors()[0].path == "home", "wrong-type error path is home");
    expect(err.errors()[0].message.find("object") != std::string::npos, "error mentions object");
    TEST_PASS("object_codec wrong type (string where object expected)");
}

void test_object_codec_nested_unknown_key_tolerant() {
    Json j = Json::object()
        .set("name", Json(std::string("alice")))
        .set("home", Json::object()
            .set("street", Json(std::string("1 Main St")))
            .set("city", Json(std::string("Springfield")))
            .set("extra", Json(int64_t{999})));    // 嵌套对象内的未知键
    Employee out; ds::ErrorList err;
    expect(EmployeeJson::parse(j, out, err), "unknown key inside nested object tolerated (non-Strict)");
    expect(err.empty(), "no errors");
    TEST_PASS("object_codec nested unknown keys tolerated");
}

// 嵌套 schema 的 validate() 跨字段钩子在解析时被调用
struct NestedInv {
    int lo = 0;
    int hi = 0;
};
struct NestedInvSchema {
    using Type = NestedInv;
    static constexpr auto fields = std::tuple{
        ds::field("lo", &NestedInv::lo, ds::int_codec{}),
        ds::field("hi", &NestedInv::hi, ds::int_codec{}),
    };
    static void validate(Type& o, ds::ErrorList& err) {
        if (o.lo > o.hi) err.add("lo", "lo must be <= hi");
    }
};
struct NestedHolder {
    std::string tag;
    NestedInv range;
};
struct NestedHolderSchema {
    using Type = NestedHolder;
    static constexpr auto fields = std::tuple{
        ds::required_field("tag", &NestedHolder::tag, ds::string_codec{}),
        ds::field("range", &NestedHolder::range, ds::object_codec<NestedInvSchema>{}),
    };
};
using NestedHolderJson = ds::json::Schema<NestedHolderSchema>;

void test_object_codec_nested_validate_hook() {
    Json okj = Json::object()
        .set("tag", Json(std::string("x")))
        .set("range", Json::object().set("lo", Json(int64_t{1})).set("hi", Json(int64_t{5})));
    NestedHolder ok; ds::ErrorList eok;
    expect(NestedHolderJson::parse(okj, ok, eok), "valid nested object parse ok");
    expect(eok.empty(), "no errors when nested invariant holds");
    expect(ok.range.lo == 1 && ok.range.hi == 5, "nested values roundtrip");
    Json badj = Json::object()
        .set("tag", Json(std::string("x")))
        .set("range", Json::object().set("lo", Json(int64_t{9})).set("hi", Json(int64_t{2})));
    NestedHolder bado; ds::ErrorList eb;
    expect(!NestedHolderJson::parse(badj, bado, eb), "nested validate hook fails parse");
    bool has_lo = false;
    for (const auto& fe : eb.errors()) if (fe.path == "lo") has_lo = true;
    expect(has_lo, "nested validate error recorded");
    TEST_PASS("object_codec nested validate hook invoked");
}

void test_object_codec_csv_rejected() {
    // CSV 无嵌套对象表示：to_csv 抛错防静默丢失（同 json_codec）；from_csv 记错拒绝。
    expect_throws_as<std::logic_error>([&] {
        std::string cell;
        ds::object_codec<AddrSchema>{}.to_csv(Addr{"a", "b"}, cell);
    }, "to_csv throws (no silent placeholder)");
    Addr out; ds::ErrorList err;
    expect(!ds::object_codec<AddrSchema>{}.from_csv("a;b", out, err, "home"),
           "from_csv rejects nested object");
    expect(err.size() == 1 && err.errors()[0].path == "home", "from_csv error path");
    TEST_PASS("object_codec CSV rejection");
}

// 嵌套对象在 optional 中（Phase-2：inventory target 可能为可选）
struct EmpOpt {
    std::string name;
    std::optional<Addr> home;
};
struct EmpOptSchema {
    using Type = EmpOpt;
    static constexpr auto fields = std::tuple{
        ds::required_field("name", &EmpOpt::name, ds::string_codec{}),
        ds::field("home", &EmpOpt::home, ds::optional_codec<ds::object_codec<AddrSchema>>{}),
    };
};
using EmpOptJson = ds::json::Schema<EmpOptSchema>;

void test_object_codec_optional_nested() {
    EmpOpt e{"bob", Addr{"2 Oak", "Metropolis"}};
    Json j = EmpOptJson::serialize(e);
    expect(j.has("home") && j["home"].type() == JsonType::Object, "optional nested serialized as object");
    EmpOpt out; ds::ErrorList err;
    expect(EmpOptJson::parse(j, out, err) && out.home.has_value() &&
           out.home->street == "2 Oak", "optional nested roundtrip");
    // 显式 null → 空 optional
    Json jn = Json::parse("{\"name\":\"bob\",\"home\":null}");
    EmpOpt out2; ds::ErrorList err2;
    expect(EmpOptJson::parse(jn, out2, err2) && !out2.home.has_value(),
           "null → empty optional");
    TEST_PASS("object_codec nested in optional");
}

// 深层嵌套：object_codec<object_codec<X>> 递归组合
struct DeepLeaf {
    int v = 0;
};
struct DeepLeafSchema {
    using Type = DeepLeaf;
    static constexpr auto fields = std::tuple{
        ds::field("v", &DeepLeaf::v, ds::int_codec{}),
    };
};
struct DeepMid {
    DeepLeaf leaf;
};
struct DeepMidSchema {
    using Type = DeepMid;
    static constexpr auto fields = std::tuple{
        ds::required_field("leaf", &DeepMid::leaf, ds::object_codec<DeepLeafSchema>{}),
    };
};
struct DeepOuter {
    DeepMid mid;
};
struct DeepOuterSchema {
    using Type = DeepOuter;
    static constexpr auto fields = std::tuple{
        ds::required_field("mid", &DeepOuter::mid, ds::object_codec<DeepMidSchema>{}),
    };
};
using DeepOuterJson = ds::json::Schema<DeepOuterSchema>;

void test_object_codec_deep_nesting() {
    DeepOuter d{{{42}}};
    Json j = DeepOuterJson::serialize(d);
    expect(j.has("mid") && j["mid"].has("leaf") && j["mid"]["leaf"]["v"].as<int64_t>() == 42,
           "deep nested serialized");
    DeepOuter out; ds::ErrorList err;
    expect(DeepOuterJson::parse(j, out, err) && out.mid.leaf.v == 42, "deep nested roundtrip");
    TEST_PASS("object_codec deep nesting");
}

// 对象数组：内层错误路径带 [i] 下标前缀，可区分元素（防 items[0] vs items[1] 歧义，review M3）
struct Row {
    std::string name;
};
struct RowSchema {
    using Type = Row;
    static constexpr auto fields = std::tuple{
        ds::required_field("name", &Row::name, ds::string_codec{}),
    };
};
struct Grid {
    std::vector<Row> rows;
};
struct GridSchema {
    using Type = Grid;
    static constexpr auto fields = std::tuple{
        ds::field("rows", &Grid::rows, ds::vector_codec<ds::object_codec<RowSchema>>{}),
    };
};
using GridJson = ds::json::Schema<GridSchema>;

void test_object_codec_array_error_path_prefix() {
    Json j = Json::parse(R"({"rows":[{"name":"a"},{}]})");
    Grid out; ds::ErrorList err;
    expect(!GridJson::parse(j, out, err), "element missing required fails");
    bool has_ix = false;
    for (const auto& fe : err.errors()) if (fe.path == "rows[1].name") has_ix = true;
    expect(has_ix, "array element error path is rows[1].name");
    TEST_PASS("object_codec array element error path prefixed");
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
    test_int_narrow_type_guard();
    test_int_double_bounds_no_ub();
    test_csv_trailing_separator_no_spurious();
    test_float_csv_roundtrip_exact();
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
    test_csv_header_and_roundtrip();
    test_csv_column_missing_optional();
    test_csv_set_join_and_quotes();
    test_csv_full_text_roundtrip();
    test_csv_required_empty_string_roundtrip();
    test_min_cost_dual_form();
    test_enchlike_json_roundtrip();
    test_enchlike_platform_legacy_alias();
    test_enchlike_csv_roundtrip();
    test_csv_presence_zero_counts_as_present();
    test_presence_flag_absent();
    test_presence_flag_cleared_on_failed_parse();
    test_set_codec_int_roundtrip();
    test_nsid_set_roundtrip();
    test_custom_codec_value_roundtrip_with_conditional_emit();
    test_validate_hook();
    test_validate_hook_csv();
    test_object_codec_roundtrip();
    test_object_codec_array_roundtrip();
    test_object_codec_required_nested_missing();
    test_object_codec_wrong_type();
    test_object_codec_nested_unknown_key_tolerant();
    test_object_codec_nested_validate_hook();
    test_object_codec_csv_rejected();
    test_object_codec_optional_nested();
    test_object_codec_deep_nesting();
    test_object_codec_array_error_path_prefix();
    return print_summary();
}
