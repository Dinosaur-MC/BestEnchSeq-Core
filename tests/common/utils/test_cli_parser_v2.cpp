// tests/common/utils/test_cli_parser_v2.cpp
// Tests for the new C++20 CLI parser in common/utils/cli/

#define BESQ_TEST_MAIN
#include "common/utils/cli/CLIFormatter.h"
#include "common/utils/cli/CLIParser.hpp"
#include "framework/test_framework.h"

using namespace cli;

#include <iostream>
#include <string>
#include <vector>

// ============================================================================
// Test option table -- covers all entry types
// ============================================================================

static const auto TEST_OPTS = OptionTable{
    Flag{.long_name = "help", .short_name = 'h', .help_key = "Show help", .help_group = "Info"},
    Flag{.long_name = "verbose", .short_name = 'v', .help_key = "Verbose output", .help_group = "Info"},
    Flag{.long_name = "version", .short_name = 'V', .help_key = "Show version", .help_group = "Info"},
    Option<int>{
        .long_name = "solutions", .short_name = 's', .help_key = "Solution count", .help_group = "Basic", .default_v = 1},
    Option<std::string>{
        .long_name = "target", .short_name = 't', .help_key = "Target item", .help_group = "Basic", .required = true},
    Option<std::string>{.long_name = "source", .short_name = 'S', .help_key = "Source enchants", .help_group = "Basic"},
    Option<std::string>{.long_name = "mode",
                        .short_name = 'm',
                        .help_key = "Operation mode",
                        .help_group = "Basic",
                        .default_v = std::string("direct")},
    Positional<std::string>{.name = "input", .help_key = "Input file"},
};

// Helper: check if a specific diagnostic code exists
bool has_diag(const std::vector<Diagnostic>& diags, ParseErrorCode code) {
    for (auto& d : diags)
        if (d.code == code)
            return true;
    return false;
}

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("test_empty_args") {
    const char* argv[] = {"program"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 1));
    expect(has_diag(result.diagnostics, ParseErrorCode::required_missing),
           "empty args should report required_missing for --target");
    TEST_PASS("empty args");
}

TEST_CASE("test_basic_key_value") {
    const char* argv[] = {"prog", "--target", "diamond_sword", "--source", "sharpness=5"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 5));
    expect(result.diagnostics.empty(), "basic args should parse cleanly");
    expect(*std::get<4>(result.value) == "diamond_sword", "target should be diamond_sword");
    expect(*std::get<5>(result.value) == "sharpness=5", "source should be sharpness=5");
    TEST_PASS("basic key=value");
}

TEST_CASE("test_key_equals_value") {
    const char* argv[] = {"prog", "--target=diamond_sword", "--source=sharpness=5"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 3));
    expect(result.diagnostics.empty(), "--key=value should parse cleanly");
    expect(*std::get<4>(result.value) == "diamond_sword", "target via --key=value");
    expect(*std::get<5>(result.value) == "sharpness=5", "source via --key=value");
    TEST_PASS("--key=value form");
}

TEST_CASE("test_flags") {
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

TEST_CASE("test_short_flag_expansion") {
    const char* argv[] = {"prog", "--target", "x", "-hVv"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 4));
    expect(result.diagnostics.empty(), "-hVv should expand cleanly");
    expect(std::get<0>(result.value) == true, "-h should be true");
    expect(std::get<1>(result.value) == true, "-v should be true");
    expect(std::get<2>(result.value) == true, "-V should be true");
    TEST_PASS("-abc flag expansion");
}

TEST_CASE("test_short_option_with_value") {
    const char* argv[] = {"prog", "-t", "diamond_sword", "-s", "3"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 5));
    expect(result.diagnostics.empty(), "-t value should parse");
    expect(*std::get<4>(result.value) == "diamond_sword", "-t should set target");
    expect(std::get<3>(result.value).has_value() && *std::get<3>(result.value) == 3, "-s 3 should set solutions to 3");
    TEST_PASS("short option with value");
}

TEST_CASE("test_inline_short_value") {
    const char* argv[] = {"prog", "-t", "x", "-s5"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 4));
    expect(result.diagnostics.empty(), "-s5 (inline value) should parse");
    expect(*std::get<3>(result.value) == 5, "-s5 should set solutions=5");
    TEST_PASS("inline short value -xN");
}

TEST_CASE("test_default_values") {
    const char* argv[] = {"prog", "--target", "sword"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 3));
    expect(result.diagnostics.empty(), "args with defaults should parse");
    expect(*std::get<3>(result.value) == 1, "solutions default should be 1");
    expect(*std::get<6>(result.value) == "direct", "mode default should be direct");
    TEST_PASS("default values");
}

TEST_CASE("test_positional_arg") {
    const char* argv[] = {"prog", "--target", "x", "myfile.json"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 4));
    expect(result.diagnostics.empty(), "positional should parse");
    expect(std::get<7>(result.value).has_value(), "positional should have value");
    expect(*std::get<7>(result.value) == "myfile.json", "positional should be myfile.json");
    TEST_PASS("positional argument");
}

TEST_CASE("test_double_dash_terminator") {
    const char* argv[] = {"prog", "--target", "x", "--", "--unknown-flag", "file.txt"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 6));
    expect(std::get<7>(result.value).has_value() && *std::get<7>(result.value) == "--unknown-flag",
           "first positional after -- should be consumed");
    expect(has_diag(result.diagnostics, ParseErrorCode::unexpected_positional), "extra positional after -- should error");
    TEST_PASS("-- terminator");
}

TEST_CASE("test_error_unknown_option") {
    const char* argv[] = {"prog", "--target", "x", "--bad-option"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 4));
    expect(has_diag(result.diagnostics, ParseErrorCode::unknown_option), "unknown option should produce unknown_option");
    TEST_PASS("unknown option error");
}

TEST_CASE("test_error_missing_value") {
    const char* argv[] = {"prog", "--target"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 2));
    expect(has_diag(result.diagnostics, ParseErrorCode::missing_value), "--target without value should produce missing_value");
    TEST_PASS("missing value error");
}

TEST_CASE("test_bare_dash_as_value") {
    // 单个 `-` 是合法值（Unix stdout/stdin 惯例，如 `--export -`）——
    // 解析器只拒绝多字符 `-` 前缀 token（--foo / -f 是选项，不吞为值）。
    const char* argv[] = {"prog", "--target", "-"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 3));
    expect(result.diagnostics.empty(), "bare dash should parse as a value");
    expect(std::get<4>(result.value).has_value() && *std::get<4>(result.value) == "-",
           "--target - should bind '-' as the value");

    const char* argv2[] = {"prog", "--target", "--foo"};
    auto result2 = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv2, 3));
    expect(has_diag(result2.diagnostics, ParseErrorCode::missing_value),
           "--target --foo should produce missing_value (option not swallowed)");
    TEST_PASS("bare dash as value");
}

TEST_CASE("test_error_invalid_value") {
    const char* argv[] = {"prog", "--target", "x", "--solutions", "abc"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 5));
    expect(has_diag(result.diagnostics, ParseErrorCode::invalid_value), "non-numeric --solutions should produce invalid_value");
    TEST_PASS("invalid value error");
}

TEST_CASE("test_error_required_missing") {
    const char* argv[] = {"prog", "--verbose"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 2));
    expect(has_diag(result.diagnostics, ParseErrorCode::required_missing), "missing --target should produce required_missing");
    TEST_PASS("required missing error");
}

TEST_CASE("test_error_accumulation") {
    const char* argv[] = {"prog", "--bad1", "--bad2", "--solutions", "notanumber"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 5));
    expect(result.diagnostics.size() >= 3, "multiple errors should all be accumulated");
    TEST_PASS("error accumulation");
}

TEST_CASE("test_default_diagnostic_formatter") {
    DefaultDiagnosticFormatter fmt;
    Diagnostic d{ParseErrorCode::unknown_option, "--bad", "--bad"};
    std::string msg = fmt(d);
    expect(msg.find("unknown option") != std::string::npos, "default formatter should mention 'unknown option'");
    TEST_PASS("default diagnostic formatter");
}

TEST_CASE("test_format_help") {
    std::string help = CLIParser(TEST_OPTS).format_help("testprog");
    expect(help.find("testprog") != std::string::npos, "help should contain program name");
    expect(help.find("--target") != std::string::npos, "help should list --target");
    expect(help.find("-h") != std::string::npos, "help should list -h");
    expect(help.find("(required)") != std::string::npos, "help should mark required options");
    expect(help.find("(default:") != std::string::npos, "help should show default values");
    TEST_PASS("format_help");
}

TEST_CASE("test_format_help_with_translator") {
    struct CapsTranslator {
        std::string operator()(std::string_view key) const { return "TRANSLATED: " + std::string(key); }
    };
    CapsTranslator tr;
    std::string help = CLIParser(TEST_OPTS, tr).format_help("prog");
    expect(help.find("TRANSLATED:") != std::string::npos, "help with translator should show translated text");
    TEST_PASS("format_help with translator");
}

TEST_CASE("test_grouped_help") {
    const auto GROUPED_OPTS = OptionTable{
        Flag{.long_name = "help", .short_name = 'h', .help_key = "Show help", .help_group = "Info"},
        Option<std::string>{.long_name = "target", .help_key = "Target item", .help_group = "Basic"},
        Option<int>{.long_name = "solutions", .short_name = 's', .help_key = "Solution count", .help_group = "Basic"},
    };
    std::string help = CLIParser(GROUPED_OPTS).format_help("prog");
    expect(help.find("--- Info ---") != std::string::npos, "grouped help should have Info header");
    expect(help.find("--- Basic ---") != std::string::npos, "grouped help should have Basic header");
    expect(help.find("--target") != std::string::npos, "grouped help should list --target");
    TEST_PASS("test_grouped_help");
}

TEST_CASE("test_duplicate_option") {
    const char* argv[] = {"prog", "--target", "x", "--target", "y"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 5));
    expect(has_diag(result.diagnostics, ParseErrorCode::duplicate_option), "duplicate --target should produce warning");
    TEST_PASS("test_duplicate_option");
}

TEST_CASE("test_flag_with_value_rejected") {
    {
        // `--help=x`: a flag cannot take a value; the value used to be silently
        // discarded and the flag still set.
        const char* argv[] = {"prog", "--help=x"};
        auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 2));
        expect(has_diag(result.diagnostics, ParseErrorCode::flag_takes_no_value),
               "--help=x should produce flag_takes_no_value");
        expect(std::get<0>(result.value) == false, "flag must NOT be set when its value was rejected");
    }
    {
        // `-hs`: h is a flag, s is a value option.  The trailing 's' used to be
        // silently dropped (help set, solutions untouched, no diagnostic).
        const char* argv[] = {"prog", "--target", "x", "-hs"};
        auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 4));
        expect(has_diag(result.diagnostics, ParseErrorCode::flag_takes_no_value), "-hs should produce flag_takes_no_value");
        expect(std::get<0>(result.value) == false, "-h must NOT be set when grouped with a value option");
        expect(std::get<3>(result.value).value_or(-1) == 1, "-s must NOT consume the trailing char");
    }
    TEST_PASS("flag with value rejected");
}

TEST_CASE("test_empty_equals_value_rejected") {
    const char* argv[] = {"prog", "--target="};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 2));
    expect(has_diag(result.diagnostics, ParseErrorCode::missing_value), "--target= (empty value) should produce missing_value");
    TEST_PASS("empty --opt= rejected");
}

TEST_CASE("test_dash_prefix_equals_value_accepted") {
    const char* argv[] = {"prog", "--target=--foo"};
    auto result = CLIParser(TEST_OPTS).parse(std::span<const char*>(argv, 2));
    expect(result.diagnostics.empty(), "--target=--foo should parse cleanly");
    expect(std::get<4>(result.value).has_value() && *std::get<4>(result.value) == "--foo",
           "--target=--foo should bind '--foo' as the value");
    TEST_PASS("--target=--foo accepted");
}

TEST_CASE("test_ungrouped_fallback") {
    // TEST_OPTS has no help_group set on the Positional → should render as flat list
    std::string help = CLIParser(TEST_OPTS).format_help("prog");
    expect(help.find("--target") != std::string::npos, "ungrouped fallback should still show --target");
    expect(help.find("--help") != std::string::npos, "ungrouped fallback should show --help");
    TEST_PASS("test_ungrouped_fallback");
}

TEST_CASE("test_subcmd_type_layer") {
    // 值类型映射：Command 槽位 = optional<嵌套 ParseResult>
    static_assert(std::is_same_v<OptionValue<Command<Option<std::string>, Flag>>,
                                  std::optional<ParseResult<Option<std::string>, Flag>>>);
    static_assert(std::is_same_v<OptionValue<Command<>>, std::optional<ParseResult<>>>);
    // is_command trait
    static_assert(is_command<Command<>>::value);
    static_assert(!is_command<Flag>::value);
    // ParseResult 新成员默认值
    ParseResult<Option<std::string>> r;
    expect(r.command_path.empty(), "command_path default empty");
    expect(!r.help_requested, "help_requested default false");
    expect(r.ok(), "empty result ok");
    expect(bool(r), "operator bool delegates to ok()");
    // ok()/all_messages() 递归进命令槽位（手工构造嵌套结果）
    ParseResult<Flag, Command<Flag>> top;
    ParseResult<Flag> sub;
    sub.diagnostics.push_back(Diagnostic{ParseErrorCode::unknown_command, "x", {}});
    sub.messages.push_back("nested err");
    std::get<1>(top.value) = std::move(sub);
    expect(!top.ok(), "ok() must recurse into command slots");
    auto msgs = top.all_messages();
    expect(msgs.size() == 1 && msgs[0] == "nested err", "all_messages flattens nested messages");
    // 叶子 Command<> 实例化空 OptionTable<>（回归：空包 std::array{} CTAD 地雷）
    const auto leaf = OptionTable{Command<>{.name = "leaf", .help_key = "Leaf"}};
    expect(std::get<0>(leaf.entries).name == "leaf", "leaf command table constructs and validates");
    TEST_PASS("subcmd type layer");
}

TEST_CASE("test_subcmd_unknown_command_formatter") {
    DefaultDiagnosticFormatter fmt;
    Diagnostic d{ParseErrorCode::unknown_command, "frob", {}};
    std::string msg = fmt(d);
    expect(msg.find("unknown command") != std::string::npos, "formatter mentions 'unknown command'");
    expect(msg.find("frob") != std::string::npos, "formatter includes the command name");
    TEST_PASS("unknown_command formatter");
}
