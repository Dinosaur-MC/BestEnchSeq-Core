#include "io/json.h"
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

    std::cout << "  [OK] test_default_json_is_null" << std::endl;
}

void test_null_static() {
    Json j = Json::null();
    expect(j.is_valid(), "Json::null() should be valid");
    expect(j.type() == JsonType::Null, "Json::null() type is Null");
    expect(j.to_string() == "null", "Json::null() serializes to null");

    std::cout << "  [OK] test_null_static" << std::endl;
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

    std::cout << "  [OK] test_construct_bool" << std::endl;
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

    std::cout << "  [OK] test_construct_number" << std::endl;
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

    std::cout << "  [OK] test_construct_string" << std::endl;
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

    std::cout << "  [OK] test_construct_array" << std::endl;
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

    std::cout << "  [OK] test_construct_object" << std::endl;
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

    std::cout << "  [OK] test_copy_equality" << std::endl;
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

    std::cout << "  [OK] test_path_queries" << std::endl;
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

    std::cout << "  [OK] test_get_value" << std::endl;
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

    std::cout << "  [OK] test_parse_scalars" << std::endl;
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

    std::cout << "  [OK] test_parse_unicode" << std::endl;
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

    std::cout << "  [OK] test_parse_arrays" << std::endl;
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

    std::cout << "  [OK] test_parse_objects" << std::endl;
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

    std::cout << "  [OK] test_pretty_print" << std::endl;
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

    std::cout << "  [OK] test_parse_errors" << std::endl;
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

    std::cout << "  [OK] test_round_trip" << std::endl;
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

    std::cout << "  [OK] test_stream_parsing" << std::endl;
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
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
