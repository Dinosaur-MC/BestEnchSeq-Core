#include "domain/interface/cli/CLIApp.h"
#include "domain/interface/cli/EnchParser.h"
#include "domain/interface/cli/ItemParser.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/types/EnchInfo.h"
#include "framework/test_utils.h"

#include <iostream>
#include <stdexcept>

namespace {

// ============================================================================
// Shared registries for parser tests
// ============================================================================
struct TestRegistries {
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;

    TestRegistries() {
        eq_reg.insert(Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword",
                                NSID("#minecraft:sword"), 1561});
        eq_reg.insert(Equipment{NSID("minecraft:sword"), "Sword",
                                NSID("#minecraft:sword"), 250});

        ench_reg.insert(EnchInfo{NSID("minecraft:sharpness"), "Sharpness",
                                 MCE::All, 5, 5, 1, false, {},
                                 {NSID("#minecraft:sword")}});
        ench_reg.insert(EnchInfo{NSID("minecraft:knockback"), "Knockback",
                                 MCE::All, 2, 2, 2, false, {},
                                 {NSID("#minecraft:sword")}});
        ench_reg.insert(EnchInfo{NSID("thermalfoundation:excavate"), "Excavate",
                                 MCE::All, 3, 3, 2, false, {},
                                 {NSID("#minecraft:sword")}});
    }
};

// ---------------------------------------------------------------------------
// Basic argument parsing
// ---------------------------------------------------------------------------

void test_basic_args() {
    const char *argv[] = {"besq", "--target", "diamond_sword", "--source", "sharpness=5"};

    auto config = CLIApp::parse(5, const_cast<char **>(argv));

    expect(config.mode == "direct", "mode should default to direct");
    expect(config.target == "diamond_sword", "target should be diamond_sword");
    expect(config.source == "sharpness=5", "source should be sharpness=5");

    std::cout << "  PASS: test_basic_args" << std::endl;
}

// ---------------------------------------------------------------------------
// Enchantment spec parsing
// ---------------------------------------------------------------------------

void test_ench_with_ns() {
    TestRegistries regs;
    auto ench_set = EnchParser::parse("minecraft:sharpness=5", regs.ench_reg);
    expect(ench_set.size() == 1, "should parse one enchantment");
    if (!ench_set.empty()) {
        auto& ench = *ench_set.begin();
        expect(ench.level == 5, "level should be 5");
    }
    std::cout << "  PASS: test_ench_with_ns" << std::endl;
}

void test_ench_without_ns() {
    TestRegistries regs;
    auto ench_set = EnchParser::parse("sharpness=5", regs.ench_reg);
    expect(ench_set.size() == 1, "should parse one enchantment");
    if (!ench_set.empty()) {
        auto& ench = *ench_set.begin();
        expect(ench.level == 5, "level should be 5");
    }
    std::cout << "  PASS: test_ench_without_ns" << std::endl;
}

void test_colon_shorthand() {
    TestRegistries regs;
    auto ench_set = EnchParser::parse("sharpness:5", regs.ench_reg);
    expect(ench_set.size() == 1, "should parse one");
    if (!ench_set.empty()) {
        auto& ench = *ench_set.begin();
        expect(ench.level == 5, "level should be 5");
    }
    std::cout << "  PASS: test_colon_shorthand" << std::endl;
}

// ---------------------------------------------------------------------------
// Target spec parsing
// ---------------------------------------------------------------------------

void test_target_with_inline() {
    TestRegistries regs;
    auto target = ItemParser::parse("diamond_sword[sharpness=3]",
                                    regs.ench_reg, regs.eq_reg);
    expect(!target.id.str().empty(), "id should be set");
    expect(target.enchantments.size() == 1, "should have one enchant");

    std::cout << "  PASS: test_target_with_inline" << std::endl;
}

void test_parse_target_no_brackets() {
    TestRegistries regs;
    auto target = ItemParser::parse("diamond_sword", regs.ench_reg, regs.eq_reg);
    expect(!target.id.str().empty(), "equipment should be set");
    expect(target.enchantments.empty(), "no enchants when no brackets");

    std::cout << "  PASS: test_parse_target_no_brackets" << std::endl;
}

// ---------------------------------------------------------------------------
// Help flag
// ---------------------------------------------------------------------------

void test_help_flag() {
    const char *argv[] = {"besq", "--help"};
    auto config = CLIApp::parse(2, const_cast<char **>(argv));
    expect(config.help == true, "--help should be true");
    std::cout << "  PASS: test_help_flag" << std::endl;
}

void test_help_short_flag() {
    const char *argv[] = {"besq", "-h"};
    auto config = CLIApp::parse(2, const_cast<char **>(argv));
    expect(config.help == true, "-h should be true");
    std::cout << "  PASS: test_help_short_flag" << std::endl;
}

void test_verbose_short_flag() {
    const char *argv[] = {"besq", "--target", "sword", "--source", "sharp=5", "-v"};
    auto config = CLIApp::parse(6, const_cast<char **>(argv));
    expect(config.verbose == true, "-v should set verbose");
    expect(config.target == "sword", "target still parsed");
    std::cout << "  PASS: test_verbose_short_flag" << std::endl;
}

// ---------------------------------------------------------------------------
// Default values
// ---------------------------------------------------------------------------

void test_default_values() {
    const char *argv[] = {"besq", "--target", "diamond_sword", "--source", "sharpness=5"};
    auto config = CLIApp::parse(5, const_cast<char **>(argv));
    expect(config.mode == "direct", "default mode should be direct");
    expect(config.format == "text", "default format should be text");
    expect(config.solutions == 1, "default solutions should be 1");
    expect(config.profile.has_value() && *config.profile == "builtin:vanilla",
           "default profile should be builtin:vanilla");
    std::cout << "  PASS: test_default_values" << std::endl;
}

// ---------------------------------------------------------------------------
// Unknown flag handling
// ---------------------------------------------------------------------------

void test_unknown_flag_throws() {
    const char *argv[] = {"besq", "--unknown_flag", "value"};
    bool threw = false;
    try {
        CLIApp::parse(3, const_cast<char **>(argv));
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
    TestRegistries regs;
    auto ench_set = EnchParser::parse("sharpness=5,knockback=2", regs.ench_reg);
    expect(ench_set.size() == 2, "should parse two enchantments");
    std::cout << "  PASS: test_enchantment_list" << std::endl;
}

// ---------------------------------------------------------------------------
// --key=value form
// ---------------------------------------------------------------------------

void test_key_value_equals_form() {
    const char *argv[] = {"besq", "--target=diamond_sword", "--source=sharpness=5"};
    auto config = CLIApp::parse(3, const_cast<char **>(argv));
    expect(config.target == "diamond_sword", "target via --key=value form");
    expect(config.source == "sharpness=5", "source via --key=value form");
    std::cout << "  PASS: test_key_value_equals_form" << std::endl;
}

// ---------------------------------------------------------------------------
// Empty source is a user error — EnchParser::parse must throw
// ---------------------------------------------------------------------------

void test_empty_enchantment_list() {
    EnchantmentRegistry empty_reg;
    // Empty/whitespace-only source is rejected with cli.err.empty_source;
    // there is no "empty list means empty EnchSet" path.
    bool threw = false;
    try {
        EnchParser::parse("", empty_reg);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    expect(threw, "empty source string should throw cli.err.empty_source");
    std::cout << "  PASS: test_empty_enchantment_list" << std::endl;
}

// ---------------------------------------------------------------------------
// Target with multiple inline enchants
// ---------------------------------------------------------------------------

void test_target_multiple_inline() {
    TestRegistries regs;
    auto target = ItemParser::parse("diamond_sword[sharpness=5,knockback=2]",
                                    regs.ench_reg, regs.eq_reg);
    expect(!target.id.str().empty(), "equipment should be set");
    expect(target.enchantments.size() == 2, "two inline enchants");
    std::cout << "  PASS: test_target_multiple_inline" << std::endl;
}

// ---------------------------------------------------------------------------
// Target with namespaced inline enchant
// ---------------------------------------------------------------------------

void test_target_with_ns_inline() {
    TestRegistries regs;
    auto target = ItemParser::parse("diamond_sword[minecraft:sharpness=3]",
                                    regs.ench_reg, regs.eq_reg);
    expect(!target.id.str().empty(), "equipment should be set");
    expect(target.enchantments.size() == 1, "one inline enchant");
    std::cout << "  PASS: test_target_with_ns_inline" << std::endl;
}

// ---------------------------------------------------------------------------
// --solutions with valid values
// ---------------------------------------------------------------------------

void test_solutions_flag() {
    {
        const char *argv[] = {"besq", "--target", "sword", "--source", "sharp=5", "--solutions", "0"};
        auto config = CLIApp::parse(7, const_cast<char **>(argv));
        expect(config.solutions == 0, "--solutions 0");
    }
    {
        const char *argv[] = {"besq", "--target", "sword", "--source", "sharp=5", "--solutions=10"};
        auto config = CLIApp::parse(6, const_cast<char **>(argv));
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
        CLIApp::parse(3, const_cast<char **>(argv));
    } catch (const std::runtime_error &) {
        threw = true;
    }
    expect(threw, "missing --target should throw");
    std::cout << "  PASS: test_missing_target_throws" << std::endl;
}

void test_source_not_required() {
    const char *argv[] = {"besq", "--target", "diamond_sword"};
    auto config = CLIApp::parse(3, const_cast<char **>(argv));
    expect(config.source.empty(), "--source should be empty when not provided");
    std::cout << "  PASS: test_source_not_required" << std::endl;
}

// ---------------------------------------------------------------------------
// Enchantment with only namespace and id (no level, no equals)
// ---------------------------------------------------------------------------

void test_ench_ns_only() {
    TestRegistries regs;
    auto ench_set = EnchParser::parse("minecraft:sharpness", regs.ench_reg);
    expect(ench_set.size() == 1, "should parse one");
    if (!ench_set.empty()) {
        auto& ench = *ench_set.begin();
        expect(ench.level == 1, "default level should be 1");
    }
    std::cout << "  PASS: test_ench_ns_only" << std::endl;
}

// ---------------------------------------------------------------------------
// Enchantment with custom namespace and level
// ---------------------------------------------------------------------------

void test_ench_custom_ns_with_level() {
    TestRegistries regs;
    auto ench_set = EnchParser::parse("thermalfoundation:excavate=3", regs.ench_reg);
    expect(ench_set.size() == 1, "should parse one");
    if (!ench_set.empty()) {
        auto& ench = *ench_set.begin();
        expect(ench.level == 3, "level should be 3");
    }
    std::cout << "  PASS: test_ench_custom_ns_with_level" << std::endl;
}

// ---------------------------------------------------------------------------
// Invalid level throws
// ---------------------------------------------------------------------------

void test_ench_invalid_level_throws() {
    EnchantmentRegistry empty_reg;
    bool threw = false;
    try {
        EnchParser::parse("sharpness=abc", empty_reg);
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
            CLIApp::parse(7, const_cast<char **>(argv));
        } catch (const std::runtime_error &) {
            threw = true;
        }
        expect(threw, "invalid --solutions 'abc' should throw");
    }
    {
        const char *argv[] = {"besq", "--target", "sword", "--source", "sharp=5", "--solutions", "-1"};
        bool threw = false;
        try {
            CLIApp::parse(7, const_cast<char **>(argv));
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
    auto config = CLIApp::parse(6, const_cast<char **>(argv));
    expect(config.target == "sword", "target parsed before --");
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
        "--import", "myreg,custom:v1"
    };
    auto config = CLIApp::parse(19, const_cast<char **>(argv));
    expect(config.target == "sword", "target");
    expect(config.source == "sharp=5", "source");
    expect(config.mode == "inventory", "mode");
    expect(config.platform == "bedrock", "platform");
    expect(config.format == "json", "format");
    expect(config.solutions == 3, "solutions");
    expect(config.input.has_value() && config.input.value() == "in.json", "input");
    expect(config.output.has_value() && config.output.value() == "out.json", "output");
    expect(config.import_files.has_value() && config.import_files.value() == "myreg,custom:v1", "import");
    std::cout << "  PASS: test_all_options" << std::endl;
}

// ---------------------------------------------------------------------------
// --source flag
// ---------------------------------------------------------------------------

void test_source_flag() {
    const char *argv[] = {"besq", "--target", "diamond_sword", "--source", "efficiency=4,unbreaking=3"};
    auto config = CLIApp::parse(5, const_cast<char **>(argv));
    expect(config.target == "diamond_sword", "target should be diamond_sword");
    expect(config.source == "efficiency=4,unbreaking=3", "source should be efficiency=4,unbreaking=3");
    std::cout << "  PASS: test_source_flag" << std::endl;
}

// ---------------------------------------------------------------------------
// --import default (nullopt when not specified)
// ---------------------------------------------------------------------------

void test_import_default_nullopt() {
    const char *argv[] = {"besq", "--target", "sword"};
    auto config = CLIApp::parse(3, const_cast<char **>(argv));
    expect(!config.import_files.has_value(),
           "--import should be nullopt when not specified");
    std::cout << "  PASS: test_import_default_nullopt" << std::endl;
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
        test_import_default_nullopt();
        test_all_options();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
