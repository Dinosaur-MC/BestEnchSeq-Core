#include "framework/test_utils.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/Enchantment.h"
#include "domain/algorithm/types/ConfigTypes.h"

#include <stdexcept>

// ---------------------------------------------------------------------------
// Helper: create a simple valid enchantment list
// ---------------------------------------------------------------------------
std::vector<EnchInfo> make_valid_enchants() {
    std::vector<EnchInfo> infos;
    infos.emplace_back(
        "minecraft:sharpness", "Sharpness", MCE::Java,
        5, 5, 1, false,
        std::unordered_set<std::string>{},
        std::unordered_set<int32_t>{}
    );
    infos.emplace_back(
        "minecraft:smite", "Smite", MCE::Java,
        5, 5, 1, false,
        std::unordered_set<std::string>{},
        std::unordered_set<int32_t>{}
    );
    return infos;
}

// ---------------------------------------------------------------------------
// test_initialize_and_get
// ---------------------------------------------------------------------------
void test_initialize_and_get() {
    EnchantmentRegistry reg;
    auto infos = make_valid_enchants();
    reg.initialize(infos);

    expect(reg.size() == 2, "should have 2 enchantments");

    // Get by index
    const auto& s0 = reg.get(0);
    expect(s0.name_id == "minecraft:sharpness", "get(0) name_id");

    const auto& s1 = reg.get(1);
    expect(s1.name_id == "minecraft:smite", "get(1) name_id");

    // Get by string
    const auto& by_name = reg.get("minecraft:sharpness");
    expect(by_name.max_level == 5, "get by string: max_level");

    std::cout << "PASS: test_initialize_and_get" << std::endl;
}

// ---------------------------------------------------------------------------
// test_get_bounds
// ---------------------------------------------------------------------------
void test_get_bounds() {
    EnchantmentRegistry reg;
    auto infos = make_valid_enchants();
    reg.initialize(infos);

    // Negative index
    bool threw = false;
    try {
        reg.get(-1);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "get(-1) should throw out_of_range");

    // Out of range index
    threw = false;
    try {
        reg.get(999);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "get(999) should throw out_of_range");

    // Unknown string
    threw = false;
    try {
        reg.get("unknown_ench");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "get(\"unknown\") should throw");

    // get_id for unknown
    expect(reg.get_id("nonexistent") == -1, "get_id(\"nonexistent\") == -1");

    std::cout << "PASS: test_get_bounds" << std::endl;
}

// ---------------------------------------------------------------------------
// test_check_validation
// ---------------------------------------------------------------------------
void test_check_validation() {
    // Valid data
    auto valid = make_valid_enchants();
    expect(EnchantmentRegistry::check_validation(valid), "valid data passes validation");

    // Empty name_id
    std::vector<EnchInfo> bad_name;
    bad_name.emplace_back(
        "", "Empty", MCE::Java,
        1, 1, 1, false,
        std::unordered_set<std::string>{},
        std::unordered_set<int32_t>{}
    );
    expect(!EnchantmentRegistry::check_validation(bad_name), "empty name should fail validation");

    // max_level <= 0
    std::vector<EnchInfo> bad_max;
    bad_max.emplace_back(
        "test", "Test", MCE::Java,
        0, 0, 1, false,
        std::unordered_set<std::string>{},
        std::unordered_set<int32_t>{}
    );
    expect(!EnchantmentRegistry::check_validation(bad_max), "max_level <= 0 should fail");

    // multiplier <= 0
    std::vector<EnchInfo> bad_mult;
    bad_mult.emplace_back(
        "test", "Test", MCE::Java,
        1, 1, 0, false,
        std::unordered_set<std::string>{},
        std::unordered_set<int32_t>{}
    );
    expect(!EnchantmentRegistry::check_validation(bad_mult), "multiplier <= 0 should fail");

    // limited_level > max_level
    std::vector<EnchInfo> bad_limited;
    bad_limited.emplace_back(
        "test", "Test", MCE::Java,
        1, 5, 1, false,
        std::unordered_set<std::string>{},
        std::unordered_set<int32_t>{}
    );
    expect(!EnchantmentRegistry::check_validation(bad_limited), "limited > max should fail");

    // exclusive_set references non-existent enchantment
    std::vector<EnchInfo> bad_excl;
    bad_excl.emplace_back(
        "test", "Test", MCE::Java,
        1, 1, 1, false,
        std::unordered_set<std::string>{"nonexistent_ench"},
        std::unordered_set<int32_t>{}
    );
    expect(!EnchantmentRegistry::check_validation(bad_excl), "bad exclusive ref should fail");

    std::cout << "PASS: test_check_validation" << std::endl;
}

// ---------------------------------------------------------------------------
// test_is_incompatible
// ---------------------------------------------------------------------------
void test_is_incompatible() {
    EnchantmentRegistry reg;
    std::vector<EnchInfo> infos;
    infos.emplace_back(
        "sharpness", "Sharpness", MCE::Java,
        5, 5, 1, false,
        std::unordered_set<std::string>{"smite", "bane_of_arthropods"},
        std::unordered_set<int32_t>{}
    );
    infos.emplace_back(
        "smite", "Smite", MCE::Java,
        5, 5, 1, false,
        std::unordered_set<std::string>{"sharpness"},
        std::unordered_set<int32_t>{}
    );
    infos.emplace_back(
        "bane_of_arthropods", "Bane of Arthropods", MCE::Java,
        5, 5, 1, false,
        std::unordered_set<std::string>{},
        std::unordered_set<int32_t>{}
    );
    infos.emplace_back(
        "unbreaking", "Unbreaking", MCE::Java,
        3, 3, 1, false,
        std::unordered_set<std::string>{},
        std::unordered_set<int32_t>{}
    );

    reg.initialize(infos);

    // sharpness and smite are incompatible (mutual exclusive_set)
    expect(reg.is_incompatible(0, 1), "sharpness and smite are incompatible");
    expect(reg.is_incompatible(1, 0), "smite and sharpness are incompatible (symmetric)");

    // sharpness and bane_of_arthropods are incompatible (sharpness lists it)
    expect(reg.is_incompatible(0, 2), "sharpness incompatible with bane_of_arthropods");
    expect(reg.is_incompatible(2, 0), "bane_of_arthropods incompatible with sharpness");

    // smite and bane should NOT be incompatible (no mutual exclusivity defined)
    expect(!reg.is_incompatible(1, 2), "smite and bane are compatible");

    // unbreaking is compatible with everything
    expect(!reg.is_incompatible(3, 0), "unbreaking compatible with sharpness");
    expect(!reg.is_incompatible(3, 1), "unbreaking compatible with smite");
    expect(!reg.is_incompatible(3, 2), "unbreaking compatible with bane");

    // Same enchantment is never incompatible with itself
    expect(!reg.is_incompatible(0, 0), "same ench is never incompatible");

    std::cout << "PASS: test_is_incompatible" << std::endl;
}

// ---------------------------------------------------------------------------
// test_exclusive_set_access
// ---------------------------------------------------------------------------
void test_exclusive_set_access() {
    EnchantmentRegistry reg;
    std::vector<EnchInfo> infos;
    infos.emplace_back(
        "sharpness", "Sharpness", MCE::Java,
        5, 5, 1, false,
        std::unordered_set<std::string>{"smite"},
        std::unordered_set<int32_t>{}
    );
    infos.emplace_back(
        "smite", "Smite", MCE::Java,
        5, 5, 1, false,
        std::unordered_set<std::string>{"sharpness"},
        std::unordered_set<int32_t>{}
    );

    reg.initialize(infos);

    const auto& excl = reg.get_exclusive_set(0);
    expect(excl.size() == 1, "exclusive_set(0) should have 1 entry");
    expect(excl.contains(1), "exclusive_set(0) should contain smite (id=1)");

    // Enchantment with no incompatibilities
    const auto& empty = reg.get_exclusive_set(99);
    expect(empty.empty(), "exclusive_set for unknown id should be empty");

    std::cout << "PASS: test_exclusive_set_access" << std::endl;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    try {
        test_initialize_and_get();
        test_get_bounds();
        test_check_validation();
        test_is_incompatible();
        test_exclusive_set_access();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
