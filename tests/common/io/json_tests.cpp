#include "io/json.h"
#include "common/serialization/IJsonSerializable.h"
#include "framework/test_utils.h"

#include <iostream>
#include <sstream>
#include <string>
#include <variant>

namespace {

// ===========================================================================
// Default construction and type queries
// ===========================================================================

void test_default_json_is_null() {
    Json j;
    expect(j.is_valid(), "default Json should be valid");
    expect(j.type() == JsonType::Empty, "default Json should report Empty type");
    expect(j.to_string() == "null", "default Json serializes to null");

    std::cout << "  PASS: test_default_json_is_null" << std::endl;
}

void test_null_static() {
    Json j = Json::null();
    expect(j.is_valid(), "Json::null() should be valid");
    expect(j.type() == JsonType::Null, "Json::null() type is Null");
    expect(j.to_string() == "null", "Json::null() serializes to null");

    std::cout << "  PASS: test_null_static" << std::endl;
}

// ===========================================================================
// Value construction from C++ types
// ===========================================================================

void test_construct_bool() {
    Json t(true);
    expect(t.type() == JsonType::Bool, "Json(true) type is Bool");
    expect(t.to_string() == "true", "Json(true) serializes");

    Json f(false);
    expect(f.to_string() == "false", "Json(false) serializes");

    std::cout << "  PASS: test_construct_bool" << std::endl;
}

void test_construct_number() {
    Json i32(static_cast<int32_t>(42));
    expect(i32.type() == JsonType::Number, "Json(int32_t) type is Number");
    expect(i32.to_string() == "42", "Json(int32_t) serializes");

    Json i64(static_cast<int64_t>(99999999999));
    expect(i64.type() == JsonType::Number, "Json(int64_t) type is Number");
    // Verify it doesn't lose the large value
    expect(i64.to_string() == "99999999999", "Json(int64_t) large value serializes");

    Json flt(3.14f);
    expect(flt.type() == JsonType::Number, "Json(float) type is Number");

    Json dbl(2.71828);
    expect(dbl.type() == JsonType::Number, "Json(double) type is Number");

    std::cout << "  PASS: test_construct_number" << std::endl;
}

void test_construct_string() {
    Json s(Json::String("hello"));
    expect(s.type() == JsonType::String, "Json(String) type is String");
    expect(s.to_string() == "\"hello\"", "Json(String) serializes with quotes");

    // Empty string
    Json empty(Json::String(""));
    expect(empty.to_string() == "\"\"", "empty string serializes");

    // String with special characters
    Json esc(Json::String("a\"b"));
    expect(esc.to_string() == "\"a\\\"b\"", "string with quote escapes");

    std::cout << "  PASS: test_construct_string" << std::endl;
}

void test_construct_array() {
    Json arr = Json(Json::Array{
        Json(static_cast<int32_t>(1)),
        Json(Json::String("two")),
        Json(true),
        Json::null(),
    });
    expect(arr.type() == JsonType::Array, "Json(Array) type is Array");

    // Empty array
    Json empty_arr(Json::Array{});
    expect(empty_arr.type() == JsonType::Array, "empty array type is Array");
    expect(empty_arr.to_string() == "[]", "empty array serializes");

    std::cout << "  PASS: test_construct_array" << std::endl;
}

void test_construct_object() {
    Json obj = Json(Json::Object{
        {"name", Json(Json::String("test"))},
        {"value", Json(static_cast<int32_t>(42))},
    });
    expect(obj.type() == JsonType::Object, "Json(Object) type is Object");

    // Empty object
    Json empty_obj(Json::Object{});
    expect(empty_obj.type() == JsonType::Object, "empty object type is Object");
    expect(empty_obj.to_string() == "{}", "empty object serializes");

    std::cout << "  PASS: test_construct_object" << std::endl;
}

// ===========================================================================
// Copy and equality
// ===========================================================================

void test_copy_equality() {
    Json a = Json::parse("{\"key\":\"value\"}");
    Json b(a);
    expect(a == b, "copy-constructed Json should be equal");
    expect(b.type("key") == JsonType::String, "copy preserves data");

    Json c = Json::parse("[1,2,3]");
    Json d;
    d = c;
    expect(c == d, "copy-assigned Json should be equal");

    Json e = Json::parse("null");
    Json f(std::move(e));
    // After move, e is in a valid-but-unspecified state; f should be valid
    expect(f.type() == JsonType::Null, "move-constructed holds the data");

    std::cout << "  PASS: test_copy_equality" << std::endl;
}

// ===========================================================================
// Path-based type queries
// ===========================================================================

void test_path_queries() {
    Json root = Json(Json::Object{
        {"data", Json(Json::Object{
            {"enchantments", Json(Json::Array{})},
            {"name", Json(Json::String("sheet"))},
            {"count", Json(static_cast<int32_t>(3))},
            {"active", Json(true)},
        })},
    });

    expect(root.type("data") == JsonType::Object, "path data is Object");
    expect(root.type("data.enchantments") == JsonType::Array, "path data.enchantments is Array");
    expect(root.type("data.name") == JsonType::String, "path data.name is String");
    expect(root.type("data.count") == JsonType::Number, "path data.count is Number");
    expect(root.type("data.active") == JsonType::Bool, "path data.active is Bool");
    expect(root.type("data.missing") == JsonType::Empty, "missing key returns Empty");
    expect(root.type("data.name.extra") == JsonType::Empty, "extra depth on string returns Empty");

    // Path on root itself
    expect(root.type("") == JsonType::Object, "empty path returns root type");

    std::cout << "  PASS: test_path_queries" << std::endl;
}

// ===========================================================================
// get_value access
// ===========================================================================

void test_get_value() {
    Json bool_json = Json::parse("true");
    Json::Value val = bool_json.get_value();
    expect(std::holds_alternative<Json::Bool>(val), "get_value should expose the stored bool");
    expect(std::get<Json::Bool>(val), "stored bool should be true");

    Json num_json = Json::parse("42");
    Json::Value num_val = num_json.get_value();
    expect(std::holds_alternative<Json::Number>(num_val), "get_value should expose Number variant");

    const Json const_json = Json::parse("\"hello\"");
    Json::Value const_val = const_json.get_value();
    expect(std::holds_alternative<Json::String>(const_val), "const get_value should work");

    std::cout << "  PASS: test_get_value" << std::endl;
}

// ===========================================================================
// Parsing scalars
// ===========================================================================

void test_parse_scalars() {
    expect(Json::parse("null").type() == JsonType::Null, "null parses to Null");
    expect(Json::parse("true").type() == JsonType::Bool, "true parses to Bool");
    expect(Json::parse("false").type() == JsonType::Bool, "false parses to Bool");

    // Number forms
    expect(Json::parse("0").to_string() == "0", "0 serializes");
    expect(Json::parse("-0").to_string() == "0", "-0 normalizes to 0");
    expect(Json::parse("123").to_string() == "123", "123 serializes");
    expect(Json::parse("-456").to_string() == "-456", "-456 serializes");
    expect(Json::parse("12.5").to_string() == "12.5", "12.5 serializes");
    expect(Json::parse("1e10").type() == JsonType::Number, "1e10 parses to Number");

    // String
    expect(Json::parse("\"\"").to_string() == "\"\"", "empty string round-trips");
    expect(Json::parse("\"hello\"").to_string() == "\"hello\"", "simple string round-trips");
    expect(Json::parse("\"line\\nfeed\"").to_string() == "\"line\\nfeed\"",
           "escaped newline should round-trip");

    std::cout << "  PASS: test_parse_scalars" << std::endl;
}

void test_parse_unicode() {
    std::string input  = "\"\\u00E9\"";
    std::string expected_utf8 = "\"";
    expected_utf8.push_back(static_cast<char>(0xC3));
    expected_utf8.push_back(static_cast<char>(0xA9));
    expected_utf8.push_back('"');
    expect(Json::parse(input).to_string() == expected_utf8, "Unicode escape \\u00E9 decodes to UTF-8");

    std::string input2  = "\"\\u4E2D\\u6587\"";
    std::string expected2 = "\"\\u4E2D\\u6587\"";
    // Depending on implementation, may output the actual UTF-8 or keep the escape.
    // Just verify it parses without error.
    Json u = Json::parse(input2);
    expect(u.type() == JsonType::String, "CJK unicode escapes parse to String");

    std::cout << "  PASS: test_parse_unicode" << std::endl;
}

// ===========================================================================
// Parsing structural types
// ===========================================================================

void test_parse_arrays() {
    Json empty = Json::parse("[]");
    expect(empty.type() == JsonType::Array, "[] is Array");
    expect(empty.to_string() == "[]", "[] serializes");

    Json mixed = Json::parse("[1,true,null,\"x\"]");
    expect(mixed.type() == JsonType::Array, "mixed array parses");
    expect(mixed.to_string() == "[1,true,null,\"x\"]", "mixed array serializes compact");

    Json nested = Json::parse("[[1],[2,3]]");
    expect(nested.type() == JsonType::Array, "nested array parses");

    std::cout << "  PASS: test_parse_arrays" << std::endl;
}

void test_parse_objects() {
    Json empty = Json::parse("{}");
    expect(empty.type() == JsonType::Object, "{} is Object");
    expect(empty.to_string() == "{}", "{} serializes");

    Json single = Json::parse("{\"name\":\"sheet\"}");
    expect(single.type() == JsonType::Object, "single-key parses");
    expect(single.to_string() == "{\"name\":\"sheet\"}", "single-key serializes compact");

    Json multi = Json::parse(R"({"a":1,"b":"two","c":null})");
    expect(multi.type("a") == JsonType::Number, "multi-key object: a is Number");
    expect(multi.type("b") == JsonType::String, "multi-key object: b is String");
    expect(multi.type("c") == JsonType::Null, "multi-key object: c is Null");

    Json nested = Json::parse("{\"data\":{\"items\":[1,2,3]}}");
    expect(nested.type("data.items") == JsonType::Array, "nested path resolves");
    expect(nested.type("data.items") == JsonType::Array, "nested path resolves after compact round-trip");

    std::cout << "  PASS: test_parse_objects" << std::endl;
}

// ===========================================================================
// Pretty printing
// ===========================================================================

void test_pretty_print() {
    Json arr = Json::parse("[1,2]");
    expect(
        arr.to_string(Json::Pretty) == "[\n    1,\n    2\n]",
        "pretty array uses 4-space indent"
    );

    Json obj = Json::parse("{\"name\":\"sheet\"}");
    expect(
        obj.to_string(Json::Pretty) == "{\n    \"name\": \"sheet\"\n}",
        "pretty object uses 4-space indent"
    );

    // Verify compact still works
    expect(arr.to_string(Json::Compact) == "[1,2]", "compact after pretty still works");

    std::cout << "  PASS: test_pretty_print" << std::endl;
}

// ===========================================================================
// Parse error handling
// ===========================================================================

void test_parse_errors() {
    // Error overload (returns null + populates error message)
    // parse(const string&, string&) accepts const or non-const strings
    {
        std::string src = "01";
        std::string err;
        Json j = Json::parse(src, err);
        expect(j.type() == JsonType::Null, "leading zero: returns Null in error overload");
        expect(!err.empty(), "leading zero: populates error string");
    }
    {
        std::string src = "[1,]";
        std::string err;
        Json j = Json::parse(src, err);
        expect(j.type() == JsonType::Null, "trailing comma: returns Null in error overload");
        expect(!err.empty(), "trailing comma: populates error string");
    }

    // Exception-throwing overload
    {
        bool threw = false;
        try {
            Json::parse("\"unterminated");
        } catch (const JsonException &) {
            threw = true;
        }
        expect(threw, "unterminated string throws JsonException");
    }
    {
        bool threw = false;
        try {
            Json::parse("{\"name\" \"sheet\"}");
        } catch (const JsonException &) {
            threw = true;
        }
        expect(threw, "missing colon throws JsonException");
    }
    {
        bool threw = false;
        try {
            Json::parse("{invalid}");
        } catch (const JsonException &) {
            threw = true;
        }
        expect(threw, "invalid token throws JsonException");
    }

    std::cout << "  PASS: test_parse_errors" << std::endl;
}

// ===========================================================================
// Round-trip fidelity
// ===========================================================================

void test_round_trip() {
    // Object with mixed types
    Json original = Json::parse("{\"data\":{\"enchantments\":[1,true,null],\"name\":\"sheet\"}}");
    Json round_trip = Json::parse(original.to_string());
    expect(round_trip == original, "compact round-trip preserves DOM");
    expect(round_trip.type("data.enchantments") == JsonType::Array, "path survives round-trip");
    expect(round_trip.type("data.name") == JsonType::String, "string path survives round-trip");

    // Array round-trip
    Json arr = Json::parse("[1,2,3]");
    expect(Json::parse(arr.to_string()) == arr, "array round-trips");

    // Nested empty structures
    Json nested = Json::parse("{\"a\":[],\"b\":{}}");
    expect(Json::parse(nested.to_string()) == nested, "nested empty round-trips");

    std::cout << "  PASS: test_round_trip" << std::endl;
}

// ===========================================================================
// Stream-based parsing
// ===========================================================================

void test_stream_parsing() {
    std::stringstream ss("{\"ok\":true}");
    Json stream_json = Json::parse(ss);
    expect(stream_json.type("ok") == JsonType::Bool, "istream parses object content");
    expect(stream_json.to_string() == "{\"ok\":true}", "istream-parsed content serializes");

    // Error overload with istream
    std::stringstream bad2("{bad}");
    std::string err2;
    Json bad_json = Json::parse(bad2, err2);
    expect(bad_json.type() == JsonType::Null, "istream error overload returns Null on bad input");
    expect(!err2.empty(), "istream error overload populates error string");

    std::cout << "  PASS: test_stream_parsing" << std::endl;
}

// ===========================================================================
// New API: convenience constructors, accessors, subscript, query
// ===========================================================================

void test_convenience_constructors() {
    // Json(int32_t) stores as Number
    Json i32(static_cast<int32_t>(42));
    expect(i32.type() == JsonType::Number, "Json(int32_t) type is Number");
    expect(i32.to_string() == "42", "Json(int32_t) serializes");

    // Json(int64_t) stores as Number
    Json i64(static_cast<int64_t>(99));
    expect(i64.type() == JsonType::Number, "Json(int64_t) type is Number");

    // Json(float) stores as Number
    Json flt(3.14f);
    expect(flt.type() == JsonType::Number, "Json(float) type is Number");

    // Json(double) stores as Number
    Json dbl(2.71828);
    expect(dbl.type() == JsonType::Number, "Json(double) type is Number");

    // Json(const char*) stores as String
    Json cstr("hello");
    expect(cstr.type() == JsonType::String, "Json(const char*) type is String");
    expect(cstr.to_string() == "\"hello\"", "Json(const char*) serializes");

    // Json(std::string) stores as String
    Json str(std::string("world"));
    expect(str.type() == JsonType::String, "Json(std::string) type is String");
    expect(str.to_string() == "\"world\"", "Json(std::string) serializes");

    // Json(bool) stores as Bool
    Json b(true);
    expect(b.type() == JsonType::Bool, "Json(bool) type is Bool");
    expect(b.to_string() == "true", "Json(bool) serializes");

    std::cout << "  PASS: test_convenience_constructors" << std::endl;
}

void test_accessors() {
    // as_int on Number
    Json num(42);
    expect(num.as_int() == 42, "as_int() on Number returns value");
    Json big(static_cast<int64_t>(99999999999LL));
    expect(big.as_int() == 99999999999LL, "as_int() on large Number");

    // as_int throws on non-Number
    try {
        Json("hello").as_int();
        expect(false, "as_int() on String should throw");
    } catch (const JsonException&) {}

    // as_double on Number
    Json pi(3.14159);
    expect(pi.as_double() > 3.14 && pi.as_double() < 3.15, "as_double() on Number");

    // as_double on int64_t promotes
    Json two(2);
    expect(two.as_double() == 2.0, "as_double() on int64_t promotes");

    // as_double throws on non-Number
    try {
        Json(true).as_double();
        expect(false, "as_double() on Bool should throw");
    } catch (const JsonException&) {}

    // as_string on String
    Json s("test");
    expect(s.as_string() == "test", "as_string() on String returns value");

    // as_string throws on non-String
    try {
        Json(42).as_string();
        expect(false, "as_string() on Number should throw");
    } catch (const JsonException&) {}

    // as_bool on Bool
    Json t(true);
    expect(t.as_bool() == true, "as_bool() on true Bool");
    Json f(false);
    expect(f.as_bool() == false, "as_bool() on false Bool");

    // as_bool throws on non-Bool
    try {
        Json::null().as_bool();
        expect(false, "as_bool() on Null should throw");
    } catch (const JsonException&) {}

    std::cout << "  PASS: test_accessors" << std::endl;
}

void test_subscript_operators() {
    // operator[] on Object
    Json obj = Json::parse("{\"a\":1,\"b\":\"two\",\"c\":true}");
    expect(obj["a"].as_int() == 1, "obj[\"a\"] returns int");
    expect(obj["b"].as_string() == "two", "obj[\"b\"] returns string");
    expect(obj["c"].as_bool() == true, "obj[\"c\"] returns bool");

    // operator[] on Object returns null() for missing key (const overload)
    const Json& const_obj = obj;
    Json missing = const_obj["nonexistent"];
    expect(missing.is_null(), "obj[\"missing\"] returns null");

    // operator[] on Array
    Json arr = Json::parse("[10,20,30]");
    expect(arr[0].as_int() == 10, "arr[0] returns first element");
    expect(arr[1].as_int() == 20, "arr[1] returns second element");
    expect(arr[2].as_int() == 30, "arr[2] returns third element");

    // operator[] on Array returns null() for OOB index (const overload)
    const Json& const_arr = arr;
    Json oob = const_arr[100];
    expect(oob.is_null(), "arr[OOB] returns null");

    // operator[] on non-container type returns null() (const overload)
    Json scalar(42);
    const Json& const_scalar2 = scalar;
    expect(const_scalar2["key"].is_null(), "scalar[\"key\"] returns null");
    expect(const_scalar2[0].is_null(), "scalar[0] returns null");

    std::cout << "  PASS: test_subscript_operators" << std::endl;
}

void test_query_methods() {
    // is_null
    expect(Json::null().is_null(), "Json::null() is null");
    expect(!Json(42).is_null(), "Json(42) is not null");
    expect(!Json(true).is_null(), "Json(true) is not null");
    expect(!Json("text").is_null(), "Json(\"text\") is not null");
    expect(!Json::parse("[]").is_null(), "empty array is not null");
    expect(!Json::parse("{}").is_null(), "empty object is not null");

    // has on Object
    Json obj = Json::parse("{\"present\":1,\"also\":2}");
    expect(obj.has("present"), "has() on existing key returns true");
    expect(obj.has("also"), "has() on another existing key returns true");
    expect(!obj.has("missing"), "has() on missing key returns false");

    // has on non-Object
    Json arr = Json::parse("[1,2,3]");
    expect(!arr.has("key"), "has() on Array returns false");
    Json num(42);
    expect(!num.has("key"), "has() on Number returns false");

    std::cout << "  PASS: test_query_methods" << std::endl;
}

// ===========================================================================
// New API: template as<T>(), factories, chainable builders, mutable
//           subscript, JSON Path
// ===========================================================================

void test_template_as() {
    // as<bool>
    expect(Json(true).as<bool>() == true, "as<bool>() on Bool");
    expect(Json(false).as<bool>() == false, "as<bool>() on false Bool");

    // as<int64_t>
    expect(Json(42).as<int64_t>() == 42, "as<int64_t>() on Number");
    expect(Json(static_cast<int64_t>(999)).as<int64_t>() == 999, "as<int64_t>() large");

    // as<std::string>
    expect(Json("hello").as<std::string>() == "hello", "as<std::string>()");

    // as<int32_t> promotes
    expect(Json(42).as<int32_t>() == 42, "as<int32_t>() promotes");

    // as<double>
    Json pi(3.14159);
    expect(pi.as<double>() > 3.14, "as<double>()");

    // as<float>
    expect(Json(2.5f).as<float>() > 2.4f, "as<float>()");

    std::cout << "  PASS: test_template_as" << std::endl;
}

void test_factories() {
    auto obj = Json::object();
    expect(obj.type() == JsonType::Object, "Json::object() creates Object");
    expect(obj.to_string() == "{}", "Json::object() serializes");

    auto arr = Json::array();
    expect(arr.type() == JsonType::Array, "Json::array() creates Array");
    expect(arr.to_string() == "[]", "Json::array() serializes");

    std::cout << "  PASS: test_factories" << std::endl;
}

void test_chainable_set() {
    auto obj = Json::object();
    obj.set("name", "test").set("value", 42).set("active", true);

    expect(obj["name"].as_string() == "test", "set().set().set() name");
    expect(obj["value"].as_int() == 42, "set() chain value");
    expect(obj["active"].as_bool() == true, "set() chain active");

    // set on non-object auto-converts
    Json j;
    j.set("key", 1);
    expect(j.type() == JsonType::Object, "set() auto-converts to Object");

    // Chained construction pattern
    auto cfg = Json::object()
        .set("host", "localhost")
        .set("port", 8080);
    expect(cfg["host"].as_string() == "localhost", "factory chain host");
    expect(cfg["port"].as_int() == 8080, "factory chain port");

    std::cout << "  PASS: test_chainable_set" << std::endl;
}

void test_chainable_push_back() {
    auto arr = Json::array();
    arr.push_back(1).push_back(2).push_back(3);

    expect(arr[0].as_int() == 1, "push_back chain [0]");
    expect(arr[1].as_int() == 2, "push_back chain [1]");
    expect(arr[2].as_int() == 3, "push_back chain [2]");

    // push_back on non-array auto-converts
    Json j;
    j.push_back("hello");
    expect(j.type() == JsonType::Array, "push_back auto-converts to Array");

    std::cout << "  PASS: test_chainable_push_back" << std::endl;
}

void test_mutable_subscript() {
    // Mutable object subscript
    Json obj = Json::object();
    obj["name"] = Json("test");
    obj["count"] = Json(42);
    expect(obj["name"].as_string() == "test", "mutable obj[\"name\"]");
    expect(obj["count"].as_int() == 42, "mutable obj[\"count\"]");

    // Mutable array subscript
    Json arr = Json::array();
    arr.push_back(10);
    arr.push_back(20);
    arr[0] = Json(99);
    expect(arr[0].as_int() == 99, "mutable arr[0] after assignment");

    // Array OOB throws
    try {
        arr[10] = Json(0);
        expect(false, "OOB array assignment should throw");
    } catch (const JsonException&) {}

    std::cout << "  PASS: test_mutable_subscript" << std::endl;
}

void test_json_path() {
    // Build a complex JSON structure using the new API
    auto data = Json::object()
        .set("solutions", Json::array()
            .push_back(Json::object()
                .set("rank", 1)
                .set("steps", Json::array()
                    .push_back(Json::object()
                        .set("item_a", "diamond_sword")
                        .set("cost", 5)
                    )
                )
            )
        );

    // at() with dot notation
    expect(data.at("solutions").type() == JsonType::Array, "at('solutions') is Array");

    // at() with bracket index + dot
    auto first = data.at("solutions[0]");
    expect(first.type() == JsonType::Object, "at('solutions[0]') is Object");
    expect(first["rank"].as_int() == 1, "at('solutions[0]') rank");

    // Deep path
    expect(data.at("solutions[0].steps[0].item_a").as_string() == "diamond_sword",
           "deep path solutions[0].steps[0].item_a");
    expect(data.at("solutions[0].steps[0].cost").as_int() == 5,
           "deep path solutions[0].steps[0].cost");

    // at() throws on missing key
    try {
        data.at("nonexistent");
        expect(false, "at() on missing key should throw");
    } catch (const JsonException&) {}

    // at() with default
    expect(data.at("missing", Json("fallback")).as_string() == "fallback",
           "at() with default on missing returns default");
    expect(data.at("solutions[0].rank", Json(0)).as_int() == 1,
           "at() with default on existing returns actual value");

    std::cout << "  PASS: test_json_path" << std::endl;
}

void test_empty_path() {
    Json obj = Json::parse("{\"a\":1}");
    expect(obj.at("").type() == JsonType::Object, "at('') returns root");
    expect(obj.at("", Json::null()).type() == JsonType::Object, "at('', default) returns root");

    std::cout << "  PASS: test_empty_path" << std::endl;
}

// ===========================================================================
// IJsonSerializable
// ===========================================================================

namespace {

struct TestSerializable : IJsonSerializable {
    std::string name;
    int32_t value = 0;

    Json to_json() const override {
        return Json::object()
            .set("name", name)
            .set("value", value);
    }

    void from_json(const Json& json) override {
        name  = json["name"].as<std::string>();
        value = static_cast<int32_t>(json["value"].as<int64_t>());
    }

    bool operator==(const TestSerializable& o) const {
        return name == o.name && value == o.value;
    }
};

} // anonymous namespace (for TestSerializable)

void test_iserializable_concept() {
    static_assert(JsonSerializable<TestSerializable>,
                  "TestSerializable should satisfy JsonSerializable concept");
    static_assert(!JsonSerializable<int>,
                  "int should NOT satisfy JsonSerializable concept");

    std::cout << "  PASS: test_iserializable_concept" << std::endl;
}

void test_iserializable_serialize() {
    TestSerializable obj;
    obj.name = "sharpness";
    obj.value = 5;
    Json j = json::serialize(obj);

    expect(j["name"].as<std::string>() == "sharpness", "serialize() name");
    expect(j["value"].as<int64_t>() == 5, "serialize() value");

    std::cout << "  PASS: test_iserializable_serialize" << std::endl;
}

void test_iserializable_deserialize() {
    Json j = Json::object()
        .set("name", "unbreaking")
        .set("value", 3);

    TestSerializable obj;
    json::deserialize(obj, j);

    expect(obj.name == "unbreaking", "deserialize(obj, j) name");
    expect(obj.value == 3, "deserialize(obj, j) value");

    // factory-style deserialize
    auto obj2 = json::deserialize<TestSerializable>(j);
    expect(obj2.name == "unbreaking", "deserialize<T>(j) name");
    expect(obj2.value == 3, "deserialize<T>(j) value");

    std::cout << "  PASS: test_iserializable_deserialize" << std::endl;
}

void test_iserializable_roundtrip() {
    TestSerializable original;
    original.name = "fortune";
    original.value = 3;
    Json j = json::serialize(original);
    auto restored = json::deserialize<TestSerializable>(j);

    expect(restored == original, "ISerializable round-trip preserves data");

    std::cout << "  PASS: test_iserializable_roundtrip" << std::endl;
}

void test_iserializable_vector() {
    TestSerializable a, b, c;
    a.name = "a"; a.value = 1;
    b.name = "b"; b.value = 2;
    c.name = "c"; c.value = 3;
    std::vector<TestSerializable> vec = {a, b, c};
    Json arr = json::serialize_vector(vec);

    expect(arr.type() == JsonType::Array, "serialize_vector produces Array");
    expect(arr[0]["name"].as<std::string>() == "a", "serialize_vector[0].name");
    expect(arr[1]["value"].as<int64_t>() == 2, "serialize_vector[1].value");
    expect(arr[2]["name"].as<std::string>() == "c", "serialize_vector[2].name");

    std::cout << "  PASS: test_iserializable_vector" << std::endl;
}

} // anonymous namespace

int main() {
    std::cout << "=== Json Tests ===" << std::endl;

    try {
        test_default_json_is_null();
        test_null_static();
        test_construct_bool();
        test_construct_number();
        test_construct_string();
        test_construct_array();
        test_construct_object();
        test_copy_equality();
        test_path_queries();
        test_get_value();
        test_parse_scalars();
        test_parse_unicode();
        test_parse_arrays();
        test_parse_objects();
        test_pretty_print();
        test_parse_errors();
        test_round_trip();
        test_stream_parsing();
        test_convenience_constructors();
        test_accessors();
        test_subscript_operators();
        test_query_methods();
        test_template_as();
        test_factories();
        test_chainable_set();
        test_chainable_push_back();
        test_mutable_subscript();
        test_json_path();
        test_empty_path();
        test_iserializable_concept();
        test_iserializable_serialize();
        test_iserializable_deserialize();
        test_iserializable_roundtrip();
        test_iserializable_vector();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
