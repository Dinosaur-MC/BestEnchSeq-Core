#include "framework/test_utils.h"
#include "registries/EquipmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "types/Equipment.h"
#include "types/EquipmentCategory.h"

#include <stdexcept>
#include <string>

// ---------------------------------------------------------------------------
// Helper: create a simple equipment list
// ---------------------------------------------------------------------------
std::vector<Equipment> make_test_equipment() {
    std::vector<Equipment> eqs;
    eqs.emplace_back(Equipment{
        "diamond_sword", "Diamond Sword",
        EquipmentCategory::ID_SWORD, 1561
    });
    eqs.emplace_back(Equipment{
        "diamond_pickaxe", "Diamond Pickaxe",
        EquipmentCategory::ID_PICKAXE, 1561
    });
    eqs.emplace_back(Equipment{
        "iron_sword", "Iron Sword",
        EquipmentCategory::ID_SWORD, 250
    });
    return eqs;
}

// ---------------------------------------------------------------------------
// test_initialize_and_get
// ---------------------------------------------------------------------------
void test_initialize_and_get() {
    EquipmentRegistry reg;
    auto eqs = make_test_equipment();
    reg.initialize(eqs);

    expect(reg.size() == 3, "should have 3 equipment entries");

    // Get by index
    const auto& e0 = reg.get(0);
    expect(e0.name_id == "diamond_sword", "get(0) name_id");

    const auto& e1 = reg.get(1);
    expect(e1.name_id == "diamond_pickaxe", "get(1) name_id");

    // Get by string
    const auto& by_name = reg.get("iron_sword");
    expect(by_name.max_durability == 250, "get by string: durability");

    std::cout << "PASS: test_initialize_and_get" << std::endl;
}

// ---------------------------------------------------------------------------
// test_get_bounds
// ---------------------------------------------------------------------------
void test_get_bounds() {
    EquipmentRegistry reg;
    auto eqs = make_test_equipment();
    reg.initialize(eqs);

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
        reg.get("unknown_equipment");
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "get(\"unknown\") should throw");

    // get_id for unknown
    expect(reg.get_id("nonexistent") == -1, "get_id(\"nonexistent\") == -1");

    std::cout << "PASS: test_get_bounds" << std::endl;
}

// ---------------------------------------------------------------------------
// test_get_by_category
// ---------------------------------------------------------------------------
void test_get_by_category() {
    EquipmentRegistry reg;
    auto eqs = make_test_equipment();
    reg.initialize(eqs);

    // Query for swords (category ID_SWORD = 1)
    auto swords = reg.get_by_category(EquipmentCategory::ID_SWORD);
    expect(swords.size() == 2, "should find 2 swords");

    // Verify both swords are found
    bool found_diamond = false;
    bool found_iron = false;
    for (const auto* s : swords) {
        if (s->name_id == "diamond_sword") found_diamond = true;
        if (s->name_id == "iron_sword") found_iron = true;
    }
    expect(found_diamond, "diamond_sword found in swords");
    expect(found_iron, "iron_sword found in swords");

    // Query for pickaxes (category ID_PICKAXE = 6)
    auto pickaxes = reg.get_by_category(EquipmentCategory::ID_PICKAXE);
    expect(pickaxes.size() == 1, "should find 1 pickaxe");
    expect(pickaxes[0]->name_id == "diamond_pickaxe", "pickaxe is diamond_pickaxe");

    // Query for non-existent category returns empty
    auto empty = reg.get_by_category(999);
    expect(empty.empty(), "unknown category returns empty vector");

    std::cout << "PASS: test_get_by_category" << std::endl;
}

// ---------------------------------------------------------------------------
// test_get_name_map
// ---------------------------------------------------------------------------
void test_get_name_map() {
    EquipmentRegistry reg;
    auto eqs = make_test_equipment();
    reg.initialize(eqs);

    auto name_map = reg.get_name_map();

    expect(name_map.size() == 3, "name_map should have 3 entries");

    auto it = name_map.find("diamond_sword");
    expect(it != name_map.end(), "diamond_sword in name_map");
    if (it != name_map.end()) {
        expect(it->second->name == "Diamond Sword", "name_map value has correct name");
        expect(it->second->category_id == EquipmentCategory::ID_SWORD, "name_map value has correct category");
    }

    // Unknown name is not in the map
    expect(name_map.find("nonexistent") == name_map.end(), "unknown name not in map");

    // Map values point to the actual Equipment objects
    const auto& reg_equip = reg.get(0);
    expect(name_map["diamond_sword"] == &reg_equip, "name_map pointer matches registry instance");

    std::cout << "PASS: test_get_name_map" << std::endl;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    try {
        // Initialize category registry with builtins (needed for some lookups)
        EquipmentCategoryRegistry cat_reg;
        cat_reg.initialize();

        test_initialize_and_get();
        test_get_bounds();
        test_get_by_category();
        test_get_name_map();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
