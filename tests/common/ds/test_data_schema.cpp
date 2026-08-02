#include "ds/ds.h"
#include "ds/Field.h"
#include "ds/codec/Codecs.h"
#include "framework/test_utils.h"
#include <string>

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
    return print_summary();
}
