#include "cli/cli.h"
#include "parsers/EnchParser.h"
#include "parsers/ItemParser.h"
#include "framework/test_utils.h"

#include <iostream>
#include <stdexcept>

namespace {

// ---------------------------------------------------------------------------
// Basic argument parsing
// ---------------------------------------------------------------------------

void test_basic_args() {
    const char *argv[] = {"besq", "--target", "diamond_sword", "--source", "sharpness=5"};

    auto config = parse_cli(5, const_cast<char **>(argv));

    expect(config.mode == "direct", "mode should default to direct");
    expect(config.target == "diamond_sword", "target should be diamond_sword");
    expect(config.source == "sharpness=5", "source should be sharpness=5");

    std::cout << "  PASS: test_basic_args" << std::endl;
}

// ---------------------------------------------------------------------------
// Enchantment spec parsing
// ---------------------------------------------------------------------------

void test_ench_with_ns() {
    auto ench_vec = EnchParser::parse("minecraft:sharpness=5");
    auto& ench = ench_vec[0];

    expect(ench.ns == "minecraft", "namespace should be minecraft");
    expect(ench.id == "sharpness", "id should be sharpness");
    expect(ench.level == 5, "level should be 5");

    std::cout << "  PASS: test_ench_with_ns" << std::endl;
}

void test_ench_without_ns() {
    auto ench_vec = EnchParser::parse("sharpness=5");
    auto& ench = ench_vec[0];

    expect(ench.ns == "minecraft", "default ns should be minecraft");
    expect(ench.id == "sharpness", "id should be sharpness");
    expect(ench.level == 5, "level should be 5");

    std::cout << "  PASS: test_ench_without_ns" << std::endl;
}

void test_colon_shorthand() {
    auto ench_vec = EnchParser::parse("sharpness:5");
    auto& ench = ench_vec[0];

    expect(ench.id == "sharpness", "id should be sharpness");
    expect(ench.level == 5, "level should be 5");

    std::cout << "  PASS: test_colon_shorthand" << std::endl;
}

// ---------------------------------------------------------------------------
// Target spec parsing
// ---------------------------------------------------------------------------

void test_target_with_inline() {
    auto target = ItemParser::parse("diamond_sword[sharpness=3]");

    expect(target.item_id == "diamond_sword", "item_id should be diamond_sword");
    expect(target.inline_enchants.size() == 1, "should have one inline enchant");
    expect(target.inline_enchants[0].id == "sharpness", "inline enchant id should be sharpness");
    expect(target.inline_enchants[0].level == 3, "inline enchant level should be 3");

    std::cout << "  PASS: test_target_with_inline" << std::endl;
}

void test_parse_target_no_brackets() {
    auto target = ItemParser::parse("diamond_sword");

    expect(target.item_id == "diamond_sword", "item_id should be diamond_sword without brackets");
    expect(target.inline_enchants.empty(), "no inline enchants when no brackets");

    std::cout << "  PASS: test_parse_target_no_brackets" << std::endl;
}

// ---------------------------------------------------------------------------
// Help flag
// ---------------------------------------------------------------------------

void test_help_flag() {
    const char *argv[] = {"besq", "--help"};

    auto config = parse_cli(2, const_cast<char **>(argv));

    expect(config.help == true, "--help should be true");

    std::cout << "  PASS: test_help_flag" << std::endl;
}

void test_help_short_flag() {
    const char *argv[] = {"besq", "-h"};

    auto config = parse_cli(2, const_cast<char **>(argv));

    expect(config.help == true, "-h should be true");

    std::cout << "  PASS: test_help_short_flag" << std::endl;
}

void test_verbose_short_flag() {
    const char *argv[] = {"besq", "--target", "sword", "--source", "sharp=5", "-v"};

    auto config = parse_cli(6, const_cast<char **>(argv));

    expect(config.verbose == true, "-v should set verbose");
    expect(config.target == "sword", "target still parsed");

    std::cout << "  PASS: test_verbose_short_flag" << std::endl;
}

// ---------------------------------------------------------------------------
// Default values
// ---------------------------------------------------------------------------

void test_default_values() {
    const char *argv[] = {"besq", "--target", "diamond_sword", "--source", "sharpness=5"};

    auto config = parse_cli(5, const_cast<char **>(argv));

    expect(config.mode == "direct", "default mode should be direct");
    expect(config.format == "text", "default format should be text");
    expect(config.solutions == 1, "default solutions should be 1");

    std::cout << "  PASS: test_default_values" << std::endl;
}

// ---------------------------------------------------------------------------
// Unknown flag handling
// ---------------------------------------------------------------------------

void test_unknown_flag_throws() {
    const char *argv[] = {"besq", "--unknown_flag", "value"};
    
    bool threw = false;

    try {
        parse_cli(3, const_cast<char **>(argv));
    } catch (const std::runtime_error &) {
        threw = true;
    }

    expect(threw, "unknown flag should throw std::runtime_error");

    std::cout << "  PASS: test_unknown_flag_throws" << std::endl;
}

// ---------------------------------------------------------------------------
// Enchantment list parsing
// ---------------------------------------------------------------------------

void test_enchantment_list() {
    auto list = EnchParser::parse("sharpness=5,knockback=2");

    expect(list.size() == 2, "should parse two enchantments");
    expect(list[0].id == "sharpness", "first enchantment id should be sharpness");
    expect(list[0].level == 5, "first enchantment level should be 5");
    expect(list[1].id == "knockback", "second enchantment id should be knockback");
    expect(list[1].level == 2, "second enchantment level should be 2");

    std::cout << "  PASS: test_enchantment_list" << std::endl;
}

// ---------------------------------------------------------------------------
// --key=value form
// ---------------------------------------------------------------------------

void test_key_value_equals_form() {
    const char *argv[] = {"besq", "--target=diamond_sword", "--source=sharpness=5"};

    auto config = parse_cli(3, const_cast<char **>(argv));

    expect(config.target == "diamond_sword", "target via --key=value form");
    expect(config.source == "sharpness=5", "source via --key=value form");

    std::cout << "  PASS: test_key_value_equals_form" << std::endl;
}

// ---------------------------------------------------------------------------
// Empty list returns empty vector
// ---------------------------------------------------------------------------

void test_empty_enchantment_list() {
    auto list = EnchParser::parse("");
    expect(list.empty(), "empty string should return empty list");

    std::cout << "  PASS: test_empty_enchantment_list" << std::endl;
}

// ---------------------------------------------------------------------------
// Target with multiple inline enchants
// ---------------------------------------------------------------------------

void test_target_multiple_inline() {
    auto target = ItemParser::parse("diamond_sword[sharpness=5,knockback=2]");

    expect(target.item_id == "diamond_sword", "item_id");
    expect(target.inline_enchants.size() == 2, "two inline enchants");
    expect(target.inline_enchants[0].id == "sharpness", "first inline id");
    expect(target.inline_enchants[0].level == 5, "first inline level");
    expect(target.inline_enchants[1].id == "knockback", "second inline id");
    expect(target.inline_enchants[1].level == 2, "second inline level");

    std::cout << "  PASS: test_target_multiple_inline" << std::endl;
}

// ---------------------------------------------------------------------------
// Target with namespaced inline enchant
// ---------------------------------------------------------------------------

void test_target_with_ns_inline() {
    auto target = ItemParser::parse("diamond_sword[minecraft:sharpness=3]");

    expect(target.item_id == "diamond_sword", "item_id");
    expect(target.inline_enchants.size() == 1, "one inline enchant");
    expect(target.inline_enchants[0].ns == "minecraft", "inline ns");
    expect(target.inline_enchants[0].id == "sharpness", "inline id");
    expect(target.inline_enchants[0].level == 3, "inline level");

    std::cout << "  PASS: test_target_with_ns_inline" << std::endl;
}

// ---------------------------------------------------------------------------
// --solutions with valid values
// ---------------------------------------------------------------------------

void test_solutions_flag() {
    {
        const char *argv[] = {"besq", "--target", "sword", "--source", "sharp=5", "--solutions", "0"};
        auto config = parse_cli(7, const_cast<char **>(argv));
        expect(config.solutions == 0, "--solutions 0");
    }
    {
        const char *argv[] = {"besq", "--target", "sword", "--source", "sharp=5", "--solutions=10"};
        auto config = parse_cli(6, const_cast<char **>(argv));
        expect(config.solutions == 10, "--solutions=10");
    }

    std::cout << "  PASS: test_solutions_flag" << std::endl;
}

// ---------------------------------------------------------------------------
// Missing target throws
// ---------------------------------------------------------------------------

void test_missing_target_throws() {
    const char *argv[] = {"besq", "--source", "sharpness=5"};

    bool threw = false;

    try {
        parse_cli(3, const_cast<char **>(argv));
    } catch (const std::runtime_error &) {
        threw = true;
    }

    expect(threw, "missing --target should throw");

    std::cout << "  PASS: test_missing_target_throws" << std::endl;
}


void test_source_not_required() {
    const char *argv[] = {"besq", "--target", "diamond_sword"};

    // --source is optional, so this should succeed
    auto config = parse_cli(3, const_cast<char **>(argv));

    expect(config.source.empty(), "--source should be empty when not provided");

    std::cout << "  PASS: test_source_not_required" << std::endl;
}

// ---------------------------------------------------------------------------
// Enchantment with only namespace and id (no level, no equals)
// ---------------------------------------------------------------------------

void test_ench_ns_only() {
    auto ench_vec = EnchParser::parse("minecraft:sharpness");
    auto& ench = ench_vec[0];

    expect(ench.ns == "minecraft", "ns");
    expect(ench.id == "sharpness", "id");
    expect(ench.level == 1, "default level should be 1");

    std::cout << "  PASS: test_ench_ns_only" << std::endl;
}

// ---------------------------------------------------------------------------
// Enchantment with custom namespace and level
// ---------------------------------------------------------------------------

void test_ench_custom_ns_with_level() {
    auto ench_vec = EnchParser::parse("thermalfoundation:excavate=3");
    auto& ench = ench_vec[0];

    expect(ench.ns == "thermalfoundation", "custom ns");
    expect(ench.id == "excavate", "id");
    expect(ench.level == 3, "level");

    std::cout << "  PASS: test_ench_custom_ns_with_level" << std::endl;
}

// ---------------------------------------------------------------------------
// Invalid level throws
// ---------------------------------------------------------------------------

void test_ench_invalid_level_throws() {
    bool threw = false;
    try {
        EnchParser::parse("sharpness=abc");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    expect(threw, "invalid level 'abc' after '=' should throw");

    std::cout << "  PASS: test_ench_invalid_level_throws" << std::endl;
}

// ---------------------------------------------------------------------------
// --solutions with invalid values throws
// ---------------------------------------------------------------------------

void test_solutions_invalid_throws() {
    {
        const char *argv[] = {"besq", "--target", "sword", "--source", "sharp=5", "--solutions", "abc"};
        bool threw = false;
        try {
            parse_cli(7, const_cast<char **>(argv));
        } catch (const std::runtime_error &) {
            threw = true;
        }
        expect(threw, "invalid --solutions 'abc' should throw");
    }
    {
        const char *argv[] = {"besq", "--target", "sword", "--source", "sharp=5", "--solutions", "-1"};
        bool threw = false;
        try {
            parse_cli(7, const_cast<char **>(argv));
        } catch (const std::runtime_error &) {
            threw = true;
        }
        expect(threw, "negative --solutions should throw");
    }

    std::cout << "  PASS: test_solutions_invalid_throws" << std::endl;
}

// ---------------------------------------------------------------------------
// -- signals end of options
// ---------------------------------------------------------------------------

void test_double_dash_stops_parsing() {
    const char *argv[] = {"besq", "--target", "sword", "--", "--source", "sharpness=5"};

    auto config = parse_cli(6, const_cast<char **>(argv));

    expect(config.target == "sword", "target parsed before --");
    // -- stops parsing, so --source is not consumed as an option
    expect(config.source.empty(), "source should be empty because -- stops parsing");

    std::cout << "  PASS: test_double_dash_stops_parsing" << std::endl;
}

// ---------------------------------------------------------------------------
// All options
// ---------------------------------------------------------------------------

void test_all_options() {
    const char *argv[] = {
        "besq", "--target", "sword", "--source", "sharp=5",
        "--mode", "inventory", "--platform", "bedrock",
        "--format", "json", "--solutions", "3",
        "--input", "in.json", "--output", "out.json",
        "--data-pack", "mypack", "--registry-dir", "myreg", "--registries", "custom:v1"
    };
    auto config = parse_cli(23, const_cast<char **>(argv));

    expect(config.target == "sword", "target");
    expect(config.source == "sharp=5", "source");
    expect(config.mode == "inventory", "mode");
    expect(config.platform == "bedrock", "platform");
    expect(config.format == "json", "format");
    expect(config.solutions == 3, "solutions");
    expect(config.input.has_value() && config.input.value() == "in.json", "input");
    expect(config.output.has_value() && config.output.value() == "out.json", "output");
    expect(config.data_pack.has_value() && config.data_pack.value() == "mypack", "data-pack");
    expect(config.registry_dir.has_value() && config.registry_dir.value() == "myreg", "registry-dir");
    expect(config.registries == "custom:v1", "registries");

    std::cout << "  PASS: test_all_options" << std::endl;
}

// ---------------------------------------------------------------------------
// --source flag
// ---------------------------------------------------------------------------

void test_source_flag() {
    const char *argv[] = {"besq", "--target", "diamond_sword", "--source", "efficiency=4,unbreaking=3"};

    auto config = parse_cli(5, const_cast<char **>(argv));

    expect(config.target == "diamond_sword", "target should be diamond_sword");
    expect(config.source == "efficiency=4,unbreaking=3", "source should be efficiency=4,unbreaking=3");

    std::cout << "  PASS: test_source_flag" << std::endl;
}

} // namespace

int main() {
    std::cout << "=== CLIParser Tests ===" << std::endl;

    try {
        test_basic_args();
        test_ench_with_ns();
        test_ench_without_ns();
        test_colon_shorthand();
        test_ench_ns_only();
        test_ench_custom_ns_with_level();
        test_ench_invalid_level_throws();
        test_solutions_invalid_throws();
        test_target_with_inline();
        test_target_with_ns_inline();
        test_parse_target_no_brackets();
        test_target_multiple_inline();
        test_help_flag();
        test_help_short_flag();
        test_verbose_short_flag();
        test_default_values();
        test_unknown_flag_throws();
        test_enchantment_list();
        test_empty_enchantment_list();
        test_key_value_equals_form();
        test_solutions_flag();
        test_missing_target_throws();
        test_source_not_required();
        test_double_dash_stops_parsing();
        test_source_flag();
        test_all_options();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
