#include "test_utils.h"
#include "parser/OutputFormatter.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/RegistryAccess.h"
#include "types/EnchInfo.h"
#include "types/Ench.h"
#include "types/EnchSet.h"
#include "types/ItemStack.h"
#include "types/Equipment.h"
#include "registries/EquipmentCategoryRegistry.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Test fixtures
// ---------------------------------------------------------------------------

Equipment diamond_sword{
    "diamond_sword", "Diamond Sword", EquipmentCategoryRegistry::ID_SWORD, 1561
};
Equipment bow{
    "bow", "Bow", EquipmentCategoryRegistry::ID_BOW, 384
};

void setup_enchinfo() {
    std::vector<EnchInfo> infos;
    infos.reserve(4);
    infos.push_back({
        "minecraft:sharpness",
        "Sharpness",
        MCE::All,
        5, 5, 1, {},
        {EquipmentCategoryRegistry::ID_SWORD},
    });
    infos.push_back({
        "minecraft:knockback",
        "Knockback",
        MCE::All,
        2, 2, 2, {},
        {EquipmentCategoryRegistry::ID_SWORD},
    });
    infos.push_back({
        "minecraft:smite",
        "Smite",
        MCE::All,
        5, 5, 1, {},
        {EquipmentCategoryRegistry::ID_SWORD},
    });
    infos.push_back({
        "minecraft:power",
        "Power",
        MCE::All,
        5, 5, 1, {},
        {EquipmentCategoryRegistry::ID_BOW},
    });
    registries::enchants().initialize(infos);
}

// ---------------------------------------------------------------------------
// Helper: build a single-solution scenario
// ---------------------------------------------------------------------------
struct TestScenario {
    EnchSolution solution;
};

TestScenario make_scenario(bool two_steps) {
    // Wanted: sharpness V + knockback II
    EnchSet wanted;
    wanted.emplace(0, 5);
    wanted.emplace(1, 2);

    // Target: fresh diamond sword
    ItemStack target(diamond_sword, EnchSet{}, 0, diamond_sword.max_durability);

    // Books
    EnchSet b1;
    b1.emplace(0, 5);
    ItemStack book1(b1, 0);

    EnchSet b2;
    b2.emplace(1, 2);
    ItemStack book2(b2, 0);

    ItemCollection avail = {book1, book2};

    // Step 1: forge sharpness V book into sword
    EnchSolution::EnchStep step1;
    step1.item_a         = target;
    step1.item_b         = book1;
    step1.exp_level_cost = 5;
    step1.exp_cost       = 55;

    std::vector<EnchSolution::EnchStep> steps = {step1};

    if (two_steps) {
        // Step 2: forge knockback II book into sword (now with sharpness V)
        EnchSet t2;
        t2.emplace(0, 5);
        ItemStack target2(diamond_sword, t2, 1, diamond_sword.max_durability);

        EnchSolution::EnchStep step2;
        step2.item_a         = target2;
        step2.item_b         = book2;
        step2.exp_level_cost = 4;
        step2.exp_cost       = 40;
        steps.push_back(step2);
    }

    EnchSolution::MetaData meta;
    meta.algorithm_name   = "test_algo";
    meta.version          = "1.0";
    meta.created_at       = 1000;
    meta.computation_time = 500;

    auto sol = EnchSolution::make(
        MCE::Java, wanted, target, avail, steps, true, meta
    );

    return {sol};
}

// ===========================================================================
// Tests
// ===========================================================================

void test_verbose_single_solution() {
    auto sc = make_scenario(true);
    std::string out = OutputFormatter::format_verbose({sc.solution}, "direct");

    // Check structural elements (ASCII-safe)
    expect(out.find("===========================================") != std::string::npos,
           "verbose_single: should contain header separator");
    expect(out.find("Step 1:") != std::string::npos,
           "verbose_single: should contain 'Step 1:'");
    expect(out.find("Step 2:") != std::string::npos,
           "verbose_single: should contain 'Step 2:'");
    expect(out.find("-------------------------------------------") != std::string::npos,
           "verbose_single: should contain step separator");
    expect(out.find("minecraft:sharpness") != std::string::npos,
           "verbose_single: should contain enchantment name");
    expect(out.find("V") != std::string::npos,
           "verbose_single: should contain Roman numeral V");

    std::cout << "  [OK] test_verbose_single_solution" << std::endl;
}

void test_verbose_multi_solution() {
    auto sc1 = make_scenario(true);
    auto sc2 = make_scenario(true);
    sc2.solution.metadata.algorithm_name = "alt_algo";

    std::string out = OutputFormatter::format_verbose({sc1.solution, sc2.solution}, "inventory");

    // Count solution separators
    size_t sep_count = 0;
    size_t pos = 0;
    while ((pos = out.find("===========================================", pos)) != std::string::npos) {
        ++sep_count;
        pos += 1;
    }
    // At least 2 header blocks (3 separators total including between solutions)
    expect(sep_count >= 3,
           "verbose_multi: should have multiple solution blocks");

    std::cout << "  [OK] test_verbose_multi_solution" << std::endl;
}

void test_describe_item_book() {
    // Test book description through compact format (avoids Chinese chars)
    EnchSet book_ench;
    book_ench.emplace(0, 5);
    ItemStack book(book_ench, 0);

    // Create a scenario where a book appears in a step (as sacrifice)
    EnchSet wanted;
    wanted.emplace(0, 5);

    ItemStack target(diamond_sword, EnchSet{}, 0, diamond_sword.max_durability);

    EnchSolution::EnchStep step;
    step.item_a         = target;
    step.item_b         = book;
    step.exp_level_cost = 5;
    step.exp_cost       = 55;

    auto sol = EnchSolution::make(
        MCE::Java, wanted, target, {book}, {step}, true,
        {"t", "1.0", 0, 0}
    );

    // Compact format uses ASCII only and should show book as "B;..."
    std::string compact = OutputFormatter::format_compact({sol}, "direct");
    expect(compact.find("B;") != std::string::npos,
           "book_desc: compact output should contain 'B;' for book items");
    expect(compact.find("minecraft:sharpness") != std::string::npos,
           "book_desc: compact output should contain enchantment name_id");

    std::cout << "  [OK] test_describe_item_book" << std::endl;
}

void test_describe_item_equipment() {
    auto sc = make_scenario(true);

    // Test through compact format
    std::string compact = OutputFormatter::format_compact({sc.solution}, "direct");
    expect(compact.find("E;") != std::string::npos,
           "equip_desc: compact output should contain 'E;' for equipment items");
    expect(compact.find("diamond_sword") != std::string::npos,
           "equip_desc: compact output should contain equipment id");

    // Also test through verbose for equipment name
    std::string verbose = OutputFormatter::format_verbose({sc.solution}, "direct");
    expect(verbose.find("Diamond Sword") != std::string::npos,
           "equip_desc: verbose output should contain equipment name");

    std::cout << "  [OK] test_describe_item_equipment" << std::endl;
}

void test_compact_format() {
    auto sc = make_scenario(true);
    std::string out = OutputFormatter::format_compact({sc.solution}, "direct");

    expect(out.find("#MODE=") != std::string::npos,
           "compact: should start with #MODE=");
    expect(out.find("#PLATFORM=") != std::string::npos,
           "compact: should contain #PLATFORM=");
    expect(out.find("|") != std::string::npos,
           "compact: should contain pipe-delimited fields");

    std::cout << "  [OK] test_compact_format" << std::endl;
}

void test_compact_multi_solution() {
    auto sc1 = make_scenario(true);
    auto sc2 = make_scenario(false); // single-step second solution

    std::string out = OutputFormatter::format_compact({sc1.solution, sc2.solution}, "inventory");

    expect(out.find("#SOLUTIONS=") != std::string::npos,
           "compact_multi: should contain #SOLUTIONS=");
    expect(out.find("===") != std::string::npos,
           "compact_multi: should contain === separator between solutions");

    std::cout << "  [OK] test_compact_multi_solution" << std::endl;
}

void test_json_round_trip() {
    auto sc = make_scenario(true);
    std::string json_out = OutputFormatter::format_json({sc.solution}, "direct");

    // Verify JSON structure
    expect(json_out.find("\"schema_version\"") != std::string::npos,
           "json: should contain schema_version");
    expect(json_out.find("\"solutions\"") != std::string::npos,
           "json: should contain solutions array");

    // Round-trip: parse the JSON string
    auto parsed = OutputFormatter::parse_json(json_out);

    expect(parsed.size() == 1,
           "json_roundtrip: should parse 1 solution");
    expect(parsed[0].is_success == sc.solution.is_success,
           "json_roundtrip: is_success should match");
    expect(parsed[0].steps.size() == sc.solution.steps.size(),
           "json_roundtrip: step count should match");
    expect(parsed[0].total_exp_level_cost == sc.solution.total_exp_level_cost,
           "json_roundtrip: total_exp_level_cost should match");
    expect(parsed[0].steps[0].exp_level_cost == sc.solution.steps[0].exp_level_cost,
           "json_roundtrip: step[0] exp_level_cost should match");
    expect(parsed[0].steps[0].exp_cost == sc.solution.steps[0].exp_cost,
           "json_roundtrip: step[0] exp_cost should match");

    std::cout << "  [OK] test_json_round_trip" << std::endl;
}

void test_json_multi_solution() {
    auto sc1 = make_scenario(true);
    auto sc2 = make_scenario(false);
    sc2.solution.metadata.algorithm_name = "alt_algo";

    std::vector<EnchSolution> solutions = {sc1.solution, sc2.solution};
    std::string json_out = OutputFormatter::format_json(solutions, "direct");

    auto parsed = OutputFormatter::parse_json(json_out);

    expect(parsed.size() == 2,
           "json_multi: should parse 2 solutions");
    expect(parsed[0].steps.size() == 2,
           "json_multi: first solution should have 2 steps");
    expect(parsed[1].steps.size() == 1,
           "json_multi: second solution should have 1 step");
    expect(parsed[0].total_exp_level_cost > 0,
           "json_multi: first solution should have positive cost");
    expect(parsed[1].total_exp_level_cost > 0,
           "json_multi: second solution should have positive cost");

    std::cout << "  [OK] test_json_multi_solution" << std::endl;
}

} // anonymous namespace

// ===========================================================================
int main() {
    std::cout << "=== OutputFormatter Tests ===" << std::endl;

    try {
        setup_enchinfo();

        test_verbose_single_solution();
        test_verbose_multi_solution();
        test_describe_item_book();
        test_describe_item_equipment();
        test_compact_format();
        test_compact_multi_solution();
        test_json_round_trip();
        test_json_multi_solution();

        std::cout << "PASS" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FATAL: " << e.what() << std::endl;
        return 1;
    }
}
