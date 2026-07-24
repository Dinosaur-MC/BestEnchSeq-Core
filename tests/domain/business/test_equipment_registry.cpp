#include "framework/test_utils.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/registries/EquipmentTagRegistry.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/EquipmentTag.h"

#include <stdexcept>
#include <string>

// ---------------------------------------------------------------------------
// Helper: create a simple equipment list
// ---------------------------------------------------------------------------
std::vector<Equipment> make_test_equipment() {
    std::vector<Equipment> eqs;
    eqs.emplace_back(Equipment{
        NSID("minecraft:diamond_sword"), "Diamond Sword",
        EquipmentTag::sword(), 1561
    });
    eqs.emplace_back(Equipment{
        NSID("minecraft:diamond_pickaxe"), "Diamond Pickaxe",
        EquipmentTag::pickaxe(), 1561
    });
    eqs.emplace_back(Equipment{
        NSID("minecraft:iron_sword"), "Iron Sword",
        EquipmentTag::sword(), 250
    });
    return eqs;
}

// ---------------------------------------------------------------------------
// test_initialize_and_get
// ---------------------------------------------------------------------------
void test_initialize_and_get() {
    auto eqs = make_test_equipment();
    EquipmentRegistry reg(eqs);

    expect(reg.size() == 3, "should have 3 equipment entries");

    // Get by index
    const auto& e0 = reg.get(0);
    expect(e0.id.str() == "minecraft:diamond_sword", "get(0) name_id");

    const auto& e1 = reg.get(1);
    expect(e1.id.str() == "minecraft:diamond_pickaxe", "get(1) name_id");

    // Get by NSID
    const auto& by_name = reg.get(NSID("minecraft:iron_sword"));
    expect(by_name.max_durability == 250, "get by NSID: durability");

    std::cout << "PASS: test_initialize_and_get" << std::endl;
}

// ---------------------------------------------------------------------------
// test_get_bounds
// ---------------------------------------------------------------------------
void test_get_bounds() {
    auto eqs = make_test_equipment();
    EquipmentRegistry reg(eqs);

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

    // Unknown NSID
    threw = false;
    try {
        reg.get(NSID("unknown_equipment"));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "get(NSID(\"unknown\")) should throw");

    // index for unknown
    expect(reg.index(NSID("nonexistent")) == IRegistry<Equipment>::nops, "index(NSID(\"nonexistent\")) == nops");

    std::cout << "PASS: test_get_bounds" << std::endl;
}

// ---------------------------------------------------------------------------
// test_get_by_category
// ---------------------------------------------------------------------------
void test_get_by_category() {
    auto eqs = make_test_equipment();
    EquipmentRegistry reg(eqs);

    // Query for swords (category EquipmentTag::sword())
    auto swords = reg.get_by_category(EquipmentTag::sword());
    expect(swords.size() == 2, "should find 2 swords");

    // Verify both swords are found
    bool found_diamond = false;
    bool found_iron = false;
    for (const auto* s : swords) {
        if (s->id.str() == "minecraft:diamond_sword") found_diamond = true;
        if (s->id.str() == "minecraft:iron_sword") found_iron = true;
    }
    expect(found_diamond, "diamond_sword found in swords");
    expect(found_iron, "iron_sword found in swords");

    // Query for pickaxes (category EquipmentTag::pickaxe())
    auto pickaxes = reg.get_by_category(EquipmentTag::pickaxe());
    expect(pickaxes.size() == 1, "should find 1 pickaxe");
    expect(pickaxes[0]->id.str() == "minecraft:diamond_pickaxe", "pickaxe is diamond_pickaxe");

    // Query for non-existent category returns empty
    auto empty = reg.get_by_category(NSID("minecraft:non_existent_category"));
    expect(empty.empty(), "unknown category returns empty vector");

    std::cout << "PASS: test_get_by_category" << std::endl;
}

// ---------------------------------------------------------------------------
// test_get_name_map
// ---------------------------------------------------------------------------
void test_get_name_map() {
    auto eqs = make_test_equipment();
    EquipmentRegistry reg(eqs);

    auto name_map = reg.get_name_map();

    expect(name_map.size() == 3, "name_map should have 3 entries");

    auto it = name_map.find(NSID("minecraft:diamond_sword"));
    expect(it != name_map.end(), "diamond_sword in name_map");
    if (it != name_map.end()) {
        expect(it->second->name == "Diamond Sword", "name_map value has correct name");
        expect(it->second->category == EquipmentTag::sword(), "name_map value has correct category");
    }

    // Unknown name is not in the map
    expect(name_map.find(NSID("nonexistent")) == name_map.end(), "unknown name not in map");

    // Map values point to the actual Equipment objects
    const auto& reg_equip = reg.get(0);
    expect(name_map[NSID("minecraft:diamond_sword")] == &reg_equip, "name_map pointer matches registry instance");

    std::cout << "PASS: test_get_name_map" << std::endl;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    try {
        // Initialize category registry (needed for some lookups)
        // Note: cat_reg is not used directly in these tests; it only served as
        // a dependency for old API compatibility.
        EquipmentTagRegistry cat_reg;

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
