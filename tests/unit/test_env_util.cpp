#include "framework/test_utils.h"
#include "utils/EnvUtil.hpp"

#include <string>

namespace {

void test_get_env_str_not_set() {
    // Variable that should not exist
    auto val = get_env_str("BESQ_TEST_VAR_THAT_DOES_NOT_EXIST_XYZ", "fallback");
    expect_eq(val, "fallback", "unset var returns default");
}

void test_get_env_str_set() {
    _putenv_s("BESQ_TEST_VAR", "hello");
    auto val = get_env_str("BESQ_TEST_VAR", "fallback");
    expect_eq(val, "hello", "set var returns its value");
    _putenv_s("BESQ_TEST_VAR", "");  // cleanup
}

void test_get_env_str_null_name() {
    auto val = get_env_str(nullptr, "fallback");
    expect_eq(val, "fallback", "null name returns default");
}

void test_get_env_str_empty_name() {
    auto val = get_env_str("", "fallback");
    expect_eq(val, "fallback", "empty name returns default");
}

void test_get_env_str_raw_pointer_lifetime() {
    // Verify that the returned string owns its data independently
    // of the getenv-internal buffer.
    _putenv_s("BESQ_EPHEMERAL", "ephemeral_value");
    std::string s1 = get_env_str("BESQ_EPHEMERAL", "fallback");
    // Overwrite the env var to check that s1 still holds the original value
    _putenv_s("BESQ_EPHEMERAL", "overwritten");
    std::string s2 = get_env_str("BESQ_EPHEMERAL", "fallback");
    expect_eq(s1, "ephemeral_value", "first string still holds original value");
    expect_eq(s2, "overwritten", "second string reads new value");
    _putenv_s("BESQ_EPHEMERAL", "");
}

// ── get_env<T> template ────────────────────────────────────────────────

void test_get_env_int64() {
    _putenv_s("BESQ_TEST_INT", "4096");
    auto val = get_env<int64_t>("BESQ_TEST_INT", 2048);
    expect_eq(val, 4096, "int64 from env var");
    _putenv_s("BESQ_TEST_INT", "");
}

void test_get_env_int64_invalid() {
    _putenv_s("BESQ_TEST_INT", "not_a_number");
    auto val = get_env<int64_t>("BESQ_TEST_INT", 2048);
    expect_eq(val, 2048, "invalid int64 returns default");
    _putenv_s("BESQ_TEST_INT", "");
}

void test_get_env_int64_unset() {
    auto val = get_env<int64_t>("BESQ_TEST_VAR_THAT_DOES_NOT_EXIST_XYZ", 1024);
    expect_eq(val, 1024, "unset int64 returns default");
}

void test_get_env_int64_overflow() {
    _putenv_s("BESQ_TEST_INT", "999999999999999999999");
    auto val = get_env<int64_t>("BESQ_TEST_INT", 100);
    expect_eq(val, 100, "overflow returns default");
    _putenv_s("BESQ_TEST_INT", "");
}

void test_get_env_int64_trailing_garbage() {
    _putenv_s("BESQ_TEST_INT", "123abc");
    auto val = get_env<int64_t>("BESQ_TEST_INT", 50);
    expect_eq(val, 50, "trailing garbage returns default");
    _putenv_s("BESQ_TEST_INT", "");
}

void test_get_env_bool_true() {
    _putenv_s("BESQ_TEST_BOOL", "true");
    expect(get_env<bool>("BESQ_TEST_BOOL", false), "true parses to true");
    _putenv_s("BESQ_TEST_BOOL", "1");
    expect(get_env<bool>("BESQ_TEST_BOOL", false), "1 parses to true");
    _putenv_s("BESQ_TEST_BOOL", "");
}

void test_get_env_bool_false() {
    _putenv_s("BESQ_TEST_BOOL", "false");
    expect(!get_env<bool>("BESQ_TEST_BOOL", true), "false parses to false");
    _putenv_s("BESQ_TEST_BOOL", "0");
    expect(!get_env<bool>("BESQ_TEST_BOOL", true), "0 parses to false");
    _putenv_s("BESQ_TEST_BOOL", "");
}

void test_get_env_bool_invalid() {
    _putenv_s("BESQ_TEST_BOOL", "maybe");
    auto val = get_env<bool>("BESQ_TEST_BOOL", true);
    expect(val, "invalid bool returns default (true)");
    _putenv_s("BESQ_TEST_BOOL", "");
}

void test_get_env_string() {
    _putenv_s("BESQ_TEST_STR", "some_string_value");
    auto val = get_env<std::string>("BESQ_TEST_STR", "fallback");
    expect_eq(val, "some_string_value", "get_env<string> reads raw value");
    _putenv_s("BESQ_TEST_STR", "");
}

void test_get_env_double() {
    _putenv_s("BESQ_TEST_DBL", "3.14");
    auto val = get_env<double>("BESQ_TEST_DBL", 0.0);
    expect_approx(val, 3.14, 0.001, "get_env<double> parses floating point");
    _putenv_s("BESQ_TEST_DBL", "");
}

// ── get_env<T> with custom converter ───────────────────────────────────

void test_get_env_with_converter() {
    _putenv_s("BESQ_TEST_CONV", "42");
    auto val = get_env<int>("BESQ_TEST_CONV", -1,
        [](std::string_view sv) { return std::stoi(std::string(sv)); });
    expect_eq(val, 42, "converter translates string to int");
    _putenv_s("BESQ_TEST_CONV", "");
}

void test_get_env_with_converter_throws() {
    _putenv_s("BESQ_TEST_CONV", "invalid");
    auto val = get_env<int>("BESQ_TEST_CONV", -1,
        [](std::string_view sv) -> int {
            // This will throw std::invalid_argument
            return std::stoi(std::string(sv));
        });
    expect_eq(val, -1, "converter exception returns default");
    _putenv_s("BESQ_TEST_CONV", "");
}

void test_get_env_with_converter_unset() {
    auto val = get_env<int>("BESQ_TEST_VAR_THAT_DOES_NOT_EXIST_XYZ", 99,
        [](std::string_view) { return 42; });
    expect_eq(val, 99, "converter not called when var is unset");
}

} // anonymous namespace

int main() {
    try {
        test_get_env_str_not_set();
        test_get_env_str_set();
        test_get_env_str_null_name();
        test_get_env_str_empty_name();
        test_get_env_str_raw_pointer_lifetime();

        test_get_env_int64();
        test_get_env_int64_invalid();
        test_get_env_int64_unset();
        test_get_env_int64_overflow();
        test_get_env_int64_trailing_garbage();

        test_get_env_bool_true();
        test_get_env_bool_false();
        test_get_env_bool_invalid();

        test_get_env_string();
        test_get_env_double();

        test_get_env_with_converter();
        test_get_env_with_converter_throws();
        test_get_env_with_converter_unset();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
