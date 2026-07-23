#include "domain/business/registries/RegistryManager.h"
#include "framework/test_utils.h"
#include <iostream>
#include <stdexcept>

namespace {

void test_add_builtin_idempotent() {
    RegistryManager mgr;
    mgr.add_builtin();
    mgr.add_builtin();  // second call must be no-op
    // Verify by loading successfully
    EquipmentTagRegistry cat_reg;
    EnchantmentRegistry ench_reg;
    EquipmentRegistry eq_reg;
    mgr.load_and_resolve(std::nullopt, cat_reg, eq_reg, ench_reg);
    expect(ench_reg.size() > 0, "builtin data should load after double add_builtin");
    std::cout << "  PASS: test_add_builtin_idempotent" << std::endl;
}

void test_scan_nonexistent_dir_throws() {
    RegistryManager mgr;
    bool threw = false;
    try {
        mgr.scan_registry_dir("nonexistent_dir_xyz_ RegistryManager_test");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "scan_registry_dir with nonexistent dir should throw");
    std::cout << "  PASS: test_scan_nonexistent_dir_throws" << std::endl;
}

void test_scan_file_not_dir_throws() {
    RegistryManager mgr;
    bool threw = false;
    try {
        mgr.scan_registry_dir("data/builtin/vanilla.json");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "scan_registry_dir with file should throw");
    std::cout << "  PASS: test_scan_file_not_dir_throws" << std::endl;
}

void test_load_empty_no_filter() {
    RegistryManager mgr;
    EquipmentTagRegistry cat_reg;
    EnchantmentRegistry ench_reg;
    EquipmentRegistry eq_reg;
    // No sources, no filter — should log WARN but not throw
    mgr.load_and_resolve(std::nullopt, cat_reg, eq_reg, ench_reg);
    std::cout << "  PASS: test_load_empty_no_filter" << std::endl;
}

void test_load_filter_not_found_throws() {
    RegistryManager mgr;
    EquipmentTagRegistry cat_reg;
    EnchantmentRegistry ench_reg;
    EquipmentRegistry eq_reg;
    bool threw = false;
    try {
        mgr.load_and_resolve("NonExistentRegistry", cat_reg, eq_reg, ench_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "filter with nonexistent name should throw");
    std::cout << "  PASS: test_load_filter_not_found_throws" << std::endl;
}

void test_load_builtin_success() {
    RegistryManager mgr;
    mgr.add_builtin();
    EquipmentTagRegistry cat_reg;
    EnchantmentRegistry ench_reg;
    EquipmentRegistry eq_reg;
    mgr.load_and_resolve(std::nullopt, cat_reg, eq_reg, ench_reg);
    expect(ench_reg.size() > 0, "builtin registry should have enchantments");
    expect(eq_reg.size() > 0, "builtin registry should have equipment");
    std::cout << "  PASS: test_load_builtin_success" << std::endl;
}

void test_load_builtin_by_name() {
    RegistryManager mgr;
    mgr.add_builtin();
    EquipmentTagRegistry cat_reg;
    EnchantmentRegistry ench_reg;
    EquipmentRegistry eq_reg;
    mgr.load_and_resolve("Vanilla", cat_reg, eq_reg, ench_reg);
    expect(ench_reg.size() > 0, "filter 'Vanilla' should load builtin enchantments");
    std::cout << "  PASS: test_load_builtin_by_name" << std::endl;
}

} // anonymous namespace

int main() {
    std::cout << "=== RegistryManager Tests ===" << std::endl;
    try {
        test_add_builtin_idempotent();
        test_scan_nonexistent_dir_throws();
        test_scan_file_not_dir_throws();
        test_load_empty_no_filter();
        test_load_filter_not_found_throws();
        test_load_builtin_success();
        test_load_builtin_by_name();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
