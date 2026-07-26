// tests/common/utils/test_cli_parser_v2.cpp
// Tests for the new C++20 CLI parser in common/utils/cli/

#include "common/utils/cli/CLIParser.h"
#include "common/utils/cli/CLIFormatter.h"
#include "framework/test_utils.h"

#include <iostream>
#include <string>
#include <vector>

// ============================================================================
// Test option table -- covers all entry types
// ============================================================================

static const auto TEST_OPTS = OptionTable{
    Flag    {.long_name = "help",    .short_name = 'h', .help_key = "Show help"},
    Flag    {.long_name = "verbose", .short_name = 'v', .help_key = "Verbose output"},
    Flag    {.long_name = "version", .short_name = 'V', .help_key = "Show version"},
    Option<int>{.long_name = "solutions", .short_name = 's', .help_key = "Solution count", .default_v = 1},
    Option<std::string>{.long_name = "target", .short_name = 't', .help_key = "Target item", .required = true},
    Option<std::string>{.long_name = "source", .short_name = 'S', .help_key = "Source enchants"},
    Option<std::string>{.long_name = "mode",   .short_name = 'm', .help_key = "Operation mode", .default_v = std::string("direct")},
    Positional<std::string>{.name = "input", .help_key = "Input file"},
};

// Helper: check if a specific diagnostic code exists
bool has_diag(const std::vector<Diagnostic>& diags, ParseErrorCode code) {
    for (auto& d : diags) if (d.code == code) return true;
    return false;
}

// ============================================================================
// Tests
// ============================================================================

void test_empty_args() {
    const char* argv[] = {"program"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 1));
    expect(has_diag(result.diagnostics, ParseErrorCode::required_missing),
           "empty args should report required_missing for --target");
    TEST_PASS("empty args");
}

void test_basic_key_value() {
    const char* argv[] = {"prog", "--target", "diamond_sword", "--source", "sharpness=5"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 5));
    expect(result.diagnostics.empty(), "basic args should parse cleanly");
    expect(*std::get<4>(result.value) == "diamond_sword", "target should be diamond_sword");
    expect(*std::get<5>(result.value) == "sharpness=5", "source should be sharpness=5");
    TEST_PASS("basic key=value");
}

void test_key_equals_value() {
    const char* argv[] = {"prog", "--target=diamond_sword", "--source=sharpness=5"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 3));
    expect(result.diagnostics.empty(), "--key=value should parse cleanly");
    expect(*std::get<4>(result.value) == "diamond_sword", "target via --key=value");
    expect(*std::get<5>(result.value) == "sharpness=5", "source via --key=value");
    TEST_PASS("--key=value form");
}

void test_flags() {
    {
        const char* argv[] = {"prog", "--target", "x", "--help"};
        auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 4));
        expect(result.diagnostics.empty(), "flag should parse");
        expect(std::get<0>(result.value) == true, "--help should be true");
    }
    {
        const char* argv[] = {"prog", "--target", "x", "-v"};
        auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 4));
        expect(result.diagnostics.empty(), "-v should parse");
        expect(std::get<1>(result.value) == true, "-v should be true");
    }
    TEST_PASS("flags");
}

void test_short_flag_expansion() {
    const char* argv[] = {"prog", "--target", "x", "-hVv"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 4));
    expect(result.diagnostics.empty(), "-hVv should expand cleanly");
    expect(std::get<0>(result.value) == true, "-h should be true");
    expect(std::get<1>(result.value) == true, "-v should be true");
    expect(std::get<2>(result.value) == true, "-V should be true");
    TEST_PASS("-abc flag expansion");
}

void test_short_option_with_value() {
    const char* argv[] = {"prog", "-t", "diamond_sword", "-s", "3"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 5));
    expect(result.diagnostics.empty(), "-t value should parse");
    expect(*std::get<4>(result.value) == "diamond_sword", "-t should set target");
    expect(std::get<3>(result.value).has_value() && *std::get<3>(result.value) == 3,
           "-s 3 should set solutions to 3");
    TEST_PASS("short option with value");
}

void test_inline_short_value() {
    const char* argv[] = {"prog", "-t", "x", "-s5"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 4));
    expect(result.diagnostics.empty(), "-s5 (inline value) should parse");
    expect(*std::get<3>(result.value) == 5, "-s5 should set solutions=5");
    TEST_PASS("inline short value -xN");
}

void test_default_values() {
    const char* argv[] = {"prog", "--target", "sword"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 3));
    expect(result.diagnostics.empty(), "args with defaults should parse");
    expect(*std::get<3>(result.value) == 1, "solutions default should be 1");
    expect(*std::get<6>(result.value) == "direct", "mode default should be direct");
    TEST_PASS("default values");
}

void test_positional_arg() {
    const char* argv[] = {"prog", "--target", "x", "myfile.json"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 4));
    expect(result.diagnostics.empty(), "positional should parse");
    expect(std::get<7>(result.value).has_value(), "positional should have value");
    expect(*std::get<7>(result.value) == "myfile.json", "positional should be myfile.json");
    TEST_PASS("positional argument");
}

void test_double_dash_terminator() {
    const char* argv[] = {"prog", "--target", "x", "--", "--unknown-flag", "file.txt"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 6));
    expect(
        std::get<7>(result.value).has_value() &&
        *std::get<7>(result.value) == "--unknown-flag",
        "first positional after -- should be consumed"
    );
    expect(has_diag(result.diagnostics, ParseErrorCode::unexpected_positional),
           "extra positional after -- should error");
    TEST_PASS("-- terminator");
}

void test_error_unknown_option() {
    const char* argv[] = {"prog", "--target", "x", "--bad-option"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 4));
    expect(has_diag(result.diagnostics, ParseErrorCode::unknown_option),
           "unknown option should produce unknown_option");
    TEST_PASS("unknown option error");
}

void test_error_missing_value() {
    const char* argv[] = {"prog", "--target"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 2));
    expect(has_diag(result.diagnostics, ParseErrorCode::missing_value),
           "--target without value should produce missing_value");
    TEST_PASS("missing value error");
}

void test_error_invalid_value() {
    const char* argv[] = {"prog", "--target", "x", "--solutions", "abc"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 5));
    expect(has_diag(result.diagnostics, ParseErrorCode::invalid_value),
           "non-numeric --solutions should produce invalid_value");
    TEST_PASS("invalid value error");
}

void test_error_required_missing() {
    const char* argv[] = {"prog", "--verbose"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 2));
    expect(has_diag(result.diagnostics, ParseErrorCode::required_missing),
           "missing --target should produce required_missing");
    TEST_PASS("required missing error");
}

void test_error_accumulation() {
    const char* argv[] = {"prog", "--bad1", "--bad2", "--solutions", "notanumber"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 5));
    expect(result.diagnostics.size() >= 3,
           "multiple errors should all be accumulated");
    TEST_PASS("error accumulation");
}

void test_default_diagnostic_formatter() {
    DefaultDiagnosticFormatter fmt;
    Diagnostic d{ParseErrorCode::unknown_option, "--bad", "--bad"};
    std::string msg = fmt(d);
    expect(msg.find("unknown option") != std::string::npos,
           "default formatter should mention 'unknown option'");
    TEST_PASS("default diagnostic formatter");
}

void test_format_help() {
    std::string help = CLIParser(TEST_OPTS).format_help("testprog");
    expect(help.find("testprog") != std::string::npos,
           "help should contain program name");
    expect(help.find("--target") != std::string::npos,
           "help should list --target");
    expect(help.find("-h") != std::string::npos,
           "help should list -h");
    expect(help.find("(required)") != std::string::npos,
           "help should mark required options");
    expect(help.find("(default:") != std::string::npos,
           "help should show default values");
    TEST_PASS("format_help");
}

void test_format_help_with_translator() {
    struct CapsTranslator {
        std::string operator()(std::string_view key) const {
            return "TRANSLATED: " + std::string(key);
        }
    };
    CapsTranslator tr;
    std::string help = CLIParser(TEST_OPTS, tr).format_help("prog");
    expect(help.find("TRANSLATED:") != std::string::npos,
           "help with translator should show translated text");
    TEST_PASS("format_help with translator");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "=== CLIParser v2 Tests ===" << std::endl;

    try {
        test_empty_args();
        test_basic_key_value();
        test_key_equals_value();
        test_flags();
        test_short_flag_expansion();
        test_short_option_with_value();
        test_inline_short_value();
        test_default_values();
        test_positional_arg();
        test_double_dash_terminator();
        test_error_unknown_option();
        test_error_missing_value();
        test_error_invalid_value();
        test_error_required_missing();
        test_error_accumulation();
        test_default_diagnostic_formatter();
        test_format_help();
        test_format_help_with_translator();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
