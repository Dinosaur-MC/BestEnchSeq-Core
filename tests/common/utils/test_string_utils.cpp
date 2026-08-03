// =============================================================================
// StringUtils tests — to_lower/to_upper/trim/split/join.
// =============================================================================

#include "framework/test_utils.h"
#include "common/utils/StringUtils.hpp"

#include <iostream>
#include <string>
#include <vector>

void test_string_utils_case() {
    expect(string_utils::to_lower("AbC") == "abc", "to_lower");
    expect(string_utils::to_upper("AbC") == "ABC", "to_upper");
    TEST_PASS("string_utils case");
}

void test_string_utils_trim() {
    expect(string_utils::trim("  hello  ") == "hello", "trim");
    expect(string_utils::trim_left("  hello") == "hello", "trim_left");
    expect(string_utils::trim_right("hello  ") == "hello", "trim_right");
    expect(string_utils::trim("\thello\n") == "hello", "trim tabs/newlines");
    TEST_PASS("string_utils trim");
}

void test_string_utils_split_join() {
    auto parts = string_utils::split("a,b,c", ",");
    expect(parts.size() == 3 && parts[0] == "a" && parts[2] == "c", "string_view split");
    expect(string_utils::join(parts, ",") == "a,b,c", "join round-trip");

    auto chars = string_utils::split("a,b", ',');
    expect(chars.size() == 2, "char split");
    expect(chars[0] == "a" && chars[1] == "b", "char split tokens");
    TEST_PASS("string_utils split/join");
}

int main() {
    try {
        test_string_utils_case();
        test_string_utils_trim();
        test_string_utils_split_join();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    }
    return print_summary();
}
