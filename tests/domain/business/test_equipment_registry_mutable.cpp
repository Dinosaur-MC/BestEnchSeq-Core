#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/types/Equipment.h"
#include "framework/test_utils.h"
#include <iostream>

namespace {

void test_add_new_equipment() {
    Equipment sword{NSID("minecraft:diamond_sword"), "Diamond Sword", NSID(), 1561};
    EquipmentRegistry reg({sword});

    Equipment new_eq{NSID("minecraft:custom_item"), "Custom", NSID(), 100};
    bool ok = reg.insert(new_eq);
    expect(ok, "insert should succeed for new equipment");
    expect(reg.size() == 2, "registry should have 2 entries");
    TEST_PASS("test_add_new_equipment");
}

void test_add_duplicate_fails() {
    Equipment sword{NSID("minecraft:diamond_sword"), "Diamond Sword", NSID(), 1561};
    EquipmentRegistry reg({sword});

    bool ok = reg.insert(sword);
    expect(!ok, "insert should fail for duplicate name_id");
    TEST_PASS("test_add_duplicate_fails");
}

void test_remove_existing() {
    Equipment sword{NSID("minecraft:diamond_sword"), "Diamond Sword", NSID(), 1561};
    EquipmentRegistry reg({sword});

    bool ok = reg.remove(NSID("minecraft:diamond_sword"));
    expect(ok, "remove should succeed for existing entry");
    expect(reg.index(NSID("minecraft:diamond_sword")) == EquipmentRegistry::nops, "removed entry should not be findable");
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
