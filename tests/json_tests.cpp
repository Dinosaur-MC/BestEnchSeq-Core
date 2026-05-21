#include "io/json.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <variant>

namespace {

void expect(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

void test_default_and_path_queries() {
    Json default_json;
    expect(default_json.is_valid(), "default Json should be valid");
    expect(default_json.type() == JsonType::Null, "default Json should report Null type");

    Json root(
        Json::Object{
            {"data", Json(
                         Json::Object{
                             {"enchantments", Json(Json::Array{})},
                             {"name", Json(Json::String("sheet"))},
                         }
                     )},
        }
    );

    expect(root.type("data") == JsonType::Object, "data should be an object");
    expect(root.type("data.enchantments") == JsonType::Array, "data.enchantments should be an array");
    expect(root.type("data.name") == JsonType::String, "data.name should be a string");
    expect(root.type("data.missing") == JsonType::Empty, "missing object key should return Empty");
    expect(root.type("data.name.extra") == JsonType::Empty, "non-object traversal should return Empty");
}

void test_scalar_parsing_and_serialization() {
    expect(Json::parse("null").type() == JsonType::Null, "null should parse to Null");
    expect(Json::parse("true").to_string() == "true", "true should serialize back to true");
    expect(Json::parse("false").to_string() == "false", "false should serialize back to false");
    expect(Json::parse("123").to_string() == "123", "123 should serialize back to 123");
    expect(Json::parse("-456").to_string() == "-456", "-456 should serialize back to -456");
    expect(Json::parse("12.5").to_string() == "12.5", "12.5 should serialize back to 12.5");
    expect(
        Json::parse("\"line\\nfeed\"").to_string() == "\"line\\nfeed\"", "escaped newline should round-trip"
    );

    std::string unicode_input = "\"\\u00E9\"";
    std::string unicode_utf8  = "\"";
    unicode_utf8.push_back(static_cast<char>(0xC3));
    unicode_utf8.push_back(static_cast<char>(0xA9));
    unicode_utf8.push_back('"');
    expect(Json::parse(unicode_input).to_string() == unicode_utf8, "Unicode escape should decode to UTF-8");
}

void test_get_value_and_scalar_errors() {
    Json bool_json    = Json::parse("true");
    Json::Value value = bool_json.get_value();
    expect(std::holds_alternative<Json::Bool>(value), "get_value should expose the stored bool");
    expect(std::get<Json::Bool>(value), "stored bool should be true");

    std::string leading_zero = "01";
    std::string leading_zero_error;
    Json leading_zero_json = Json::parse(leading_zero, leading_zero_error);
    expect(leading_zero_json.type() == JsonType::Null, "error overload should return null Json");
    expect(!leading_zero_error.empty(), "leading zero should populate an error message");

    bool unterminated_threw = false;
    try {
        Json::parse("\"unterminated");
    } catch (const JsonException &) {
        unterminated_threw = true;
    }
    expect(unterminated_threw, "unterminated string should throw JsonException");
}

void test_arrays_and_objects() {
    Json parsed = Json::parse("{\"data\":{\"enchantments\":[1,true,null],\"name\":\"sheet\"}}");
    expect(parsed.type() == JsonType::Object, "root should be an object");
    expect(parsed.type("data") == JsonType::Object, "data should be an object after parsing");
    expect(
        parsed.type("data.enchantments") == JsonType::Array, "nested array should be discoverable by path"
    );
    expect(parsed.type("data.name") == JsonType::String, "nested string should be discoverable by path");

    Json compact_array = Json::parse("[1,true,null,\"x\"]");
    expect(compact_array.to_string() == "[1,true,null,\"x\"]", "array should serialize in compact form");

    Json single_key_object = Json::parse("{\"name\":\"sheet\"}");
    expect(
        single_key_object.to_string() == "{\"name\":\"sheet\"}",
        "single-key object should serialize in compact form"
    );

    Json round_trip = Json::parse(parsed.to_string());
    expect(round_trip == parsed, "re-parsing compact output should preserve the DOM");
}

void test_structural_errors() {
    std::string trailing_comma = "[1,]";
    std::string trailing_comma_error;
    Json trailing_comma_json = Json::parse(trailing_comma, trailing_comma_error);
    expect(
        trailing_comma_json.type() == JsonType::Null, "invalid array should return null in error overload"
    );
    expect(!trailing_comma_error.empty(), "invalid array should populate an error message");

    bool missing_colon_threw = false;
    try {
        Json::parse("{\"name\" \"sheet\"}");
    } catch (const JsonException &) {
        missing_colon_threw = true;
    }
    expect(missing_colon_threw, "missing colon should throw JsonException");
}

void test_pretty_printing() {
    Json array = Json::parse("[1,2]");
    expect(
        array.to_string(Json::Pretty) == "[\n    1,\n    2\n]",
        "pretty array output should use four-space indentation"
    );

    Json object = Json::parse("{\"name\":\"sheet\"}");
    expect(
        object.to_string(Json::Pretty) == "{\n    \"name\": \"sheet\"\n}",
        "pretty object output should use four-space indentation"
    );
}

void test_stream_and_example_files() {
    std::stringstream stream("{\"ok\":true}");
    Json stream_json = Json::parse(stream);
    expect(stream_json.type("ok") == JsonType::Bool, "istream overload should parse object content");

    std::ifstream version_one("res/ench_table_v1_example.json");
    expect(version_one.is_open(), "v1 example file should open");
    Json version_one_json = Json::parse(version_one);
    expect(version_one_json.type("data") == JsonType::Object, "v1 example should expose data object");
    expect(
        version_one_json.type("data.enchantments") == JsonType::Array,
        "v1 example should expose enchantments array"
    );

    std::ifstream version_two("res/ench_table_v2_example.json");
    expect(version_two.is_open(), "v2 example file should open");
    Json version_two_json = Json::parse(version_two);
    expect(version_two_json.type("data") == JsonType::Object, "v2 example should expose data object");
    expect(
        version_two_json.type("data.enchantments") == JsonType::Array,
        "v2 example should expose enchantments array"
    );

    Json v1_round_trip = Json::parse(version_one_json.to_string());
    Json v2_round_trip = Json::parse(version_two_json.to_string());
    expect(v1_round_trip == version_one_json, "v1 example should round-trip through compact output");
    expect(v2_round_trip == version_two_json, "v2 example should round-trip through compact output");
}

} // namespace

int main() {
    test_default_and_path_queries();
    test_scalar_parsing_and_serialization();
    test_get_value_and_scalar_errors();
    test_arrays_and_objects();
    test_structural_errors();
    test_pretty_printing();
    test_stream_and_example_files();
    std::cout << "PASS json_tests" << std::endl;
    return 0;
}
