#define BESQ_TEST_MAIN
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/types/Equipment.h"
#include "framework/test_framework.h"
#include <iostream>

namespace {

TEST_CASE("test_add_new_equipment") {
    Equipment sword{NSID("minecraft:diamond_sword"), "Diamond Sword", NSID(), 1561};
    EquipmentRegistry reg({sword});

    Equipment new_eq{NSID("minecraft:custom_item"), "Custom", NSID(), 100};
    bool ok = reg.insert(new_eq).second;
    expect(ok, "insert should succeed for new equipment");
    expect(reg.size() == 2, "registry should have 2 entries");
    TEST_PASS("test_add_new_equipment");
}

TEST_CASE("test_add_duplicate_fails") {
    Equipment sword{NSID("minecraft:diamond_sword"), "Diamond Sword", NSID(), 1561};
    EquipmentRegistry reg({sword});

    bool ok = reg.insert(sword).second;
    expect(!ok, "insert should fail for duplicate name_id");
    TEST_PASS("test_add_duplicate_fails");
}

TEST_CASE("test_remove_existing") {
    Equipment sword{NSID("minecraft:diamond_sword"), "Diamond Sword", NSID(), 1561};
    EquipmentRegistry reg({sword});

    bool ok = reg.erase(NSID("minecraft:diamond_sword"));
    expect(ok, "erase should succeed for existing entry");
    expect(!reg.contains(NSID("minecraft:diamond_sword")), "removed entry should not be findable");
    TEST_PASS("test_remove_existing");
}

} // namespace
