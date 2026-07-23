#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/Enchantment.h"
#include "framework/test_utils.h"
#include <iostream>
#include <stdexcept>

namespace {

void test_add_new_enchantment() {
    EnchantmentRegistry reg;
    EnchInfo sharp{"minecraft:sharpness", "Sharpness", MCE::All, 5, 5, 1, false, {}, {}};
    reg.initialize({sharp});

    EnchInfo new_ench{"minecraft:custom_ench", "Custom", MCE::All, 3, 3, 2, false, {}, {}};
    bool ok = reg.add(new_ench);
    expect(ok, "add should succeed for new enchantment");
    expect(reg.size() == 2, "registry should have 2 entries after add");
    expect(reg.get_id("minecraft:custom_ench") >= 0, "new enchantment should be findable");
    TEST_PASS("test_add_new_enchantment");
}

void test_add_duplicate_fails() {
    EnchantmentRegistry reg;
    EnchInfo sharp{"minecraft:sharpness", "Sharpness", MCE::All, 5, 5, 1, false, {}, {}};
    reg.initialize({sharp});

    bool ok = reg.add(sharp);
    expect(!ok, "add should fail for duplicate name_id");
    TEST_PASS("test_add_duplicate_fails");
}

void test_remove_existing() {
    EnchantmentRegistry reg;
    EnchInfo sharp{"minecraft:sharpness", "Sharpness", MCE::All, 5, 5, 1, false, {}, {}};
    reg.initialize({sharp});

    bool ok = reg.remove("minecraft:sharpness");
    expect(ok, "remove should succeed for existing entry");
    expect(reg.get_id("minecraft:sharpness") < 0, "removed entry should not be findable");
    TEST_PASS("test_remove_existing");
}

void test_remove_nonexistent_fails() {
    EnchantmentRegistry reg;
    EnchInfo sharp{"minecraft:sharpness", "Sharpness", MCE::All, 5, 5, 1, false, {}, {}};
    reg.initialize({sharp});

    bool ok = reg.remove("minecraft:nonexistent");
    expect(!ok, "remove should fail for nonexistent entry");
    TEST_PASS("test_remove_nonexistent_fails");
}

void test_modify_max_level() {
    EnchantmentRegistry reg;
    EnchInfo sharp{"minecraft:sharpness", "Sharpness", MCE::All, 5, 5, 1, false, {}, {}};
    reg.initialize({sharp});

    EnchInfo patch;
    patch.max_level = 10;
    bool ok = reg.modify("minecraft:sharpness", patch);
    expect(ok, "modify should succeed");

    auto& modified = reg.get("minecraft:sharpness");
    expect(modified.max_level == 10, "max_level should be updated to 10");
    expect(modified.multiplier == 1, "multiplier should remain unchanged");
    TEST_PASS("test_modify_max_level");
}

void test_modify_nonexistent_fails() {
    EnchantmentRegistry reg;
    bool ok = reg.modify("nonexistent", EnchInfo{});
    expect(!ok, "modify should fail for nonexistent entry");
    TEST_PASS("test_modify_nonexistent_fails");
}

} // namespace

int main() {
    std::cout << "=== EnchantmentRegistry Mutable API Tests ===" << std::endl;
    try {
        test_add_new_enchantment();
        test_add_duplicate_fails();
        test_remove_existing();
        test_remove_nonexistent_fails();
        test_modify_max_level();
        test_modify_nonexistent_fails();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
