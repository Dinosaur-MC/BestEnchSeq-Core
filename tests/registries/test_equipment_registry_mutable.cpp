#include "registries/EquipmentRegistry.h"
#include "types/Equipment.h"
#include "framework/test_utils.h"
#include <iostream>

namespace {

void test_add_new_equipment() {
    EquipmentRegistry reg;
    Equipment sword{"minecraft:diamond_sword", "Diamond Sword", 0, 1561};
    reg.initialize({sword});

    Equipment new_eq{"minecraft:custom_item", "Custom", 0, 100};
    bool ok = reg.add(new_eq);
    expect(ok, "add should succeed for new equipment");
    expect(reg.size() == 2, "registry should have 2 entries");
    TEST_PASS("test_add_new_equipment");
}

void test_add_duplicate_fails() {
    EquipmentRegistry reg;
    Equipment sword{"minecraft:diamond_sword", "Diamond Sword", 0, 1561};
    reg.initialize({sword});

    bool ok = reg.add(sword);
    expect(!ok, "add should fail for duplicate name_id");
    TEST_PASS("test_add_duplicate_fails");
}

void test_remove_existing() {
    EquipmentRegistry reg;
    Equipment sword{"minecraft:diamond_sword", "Diamond Sword", 0, 1561};
    reg.initialize({sword});

    bool ok = reg.remove("minecraft:diamond_sword");
    expect(ok, "remove should succeed for existing entry");
    expect(reg.get_id("minecraft:diamond_sword") < 0, "removed entry should not be findable");
    TEST_PASS("test_remove_existing");
}

} // namespace

int main() {
    std::cout << "=== EquipmentRegistry Mutable API Tests ===" << std::endl;
    try {
        test_add_new_equipment();
        test_add_duplicate_fails();
        test_remove_existing();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
