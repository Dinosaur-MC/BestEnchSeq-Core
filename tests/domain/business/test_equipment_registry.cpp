#include "framework/test_utils.h"
#include "domain/business/registries/EquipmentRegistry.h"
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

    // Get by NSID
    const auto& e0 = reg.at(NSID("minecraft:diamond_sword"));
    expect(e0.id.str() == "minecraft:diamond_sword", "at(diamond_sword) id matches");

    const auto& e1 = reg.at(NSID("minecraft:diamond_pickaxe"));
    expect(e1.id.str() == "minecraft:diamond_pickaxe", "at(diamond_pickaxe) id matches");

    // Get by NSID
    const auto& by_name = reg.at(NSID("minecraft:iron_sword"));
    expect(by_name.max_durability == 250, "at(iron_sword): durability");

    std::cout << "PASS: test_initialize_and_get" << std::endl;
}

// ---------------------------------------------------------------------------
// test_get_bounds
// ---------------------------------------------------------------------------
void test_get_bounds() {
    auto eqs = make_test_equipment();
    EquipmentRegistry reg(eqs);

    // Unknown NSID via at() — throws out_of_range
    bool threw = false;
    try {
        reg.at(NSID("unknown_equipment"));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "at(NSID(\"unknown\")) should throw out_of_range");

    // contains for unknown
    expect(!reg.contains(NSID("nonexistent")), "contains(\"nonexistent\") == false");

    // contains for existing
    expect(reg.contains(NSID("minecraft:diamond_sword")), "contains(\"diamond_sword\") == true");

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
    for (const auto& s : swords) {
        if (s.id.str() == "minecraft:diamond_sword") found_diamond = true;
        if (s.id.str() == "minecraft:iron_sword") found_iron = true;
    }
    expect(found_diamond, "diamond_sword found in swords");
    expect(found_iron, "iron_sword found in swords");

    // Query for pickaxes (category EquipmentTag::pickaxe())
    auto pickaxes = reg.get_by_category(EquipmentTag::pickaxe());
    expect(pickaxes.size() == 1, "should find 1 pickaxe");
    expect(pickaxes[0].id.str() == "minecraft:diamond_pickaxe", "pickaxe is diamond_pickaxe");

    // Query for non-existent category returns empty
    auto empty = reg.get_by_category(NSID("minecraft:non_existent_category"));
    expect(empty.empty(), "unknown category returns empty vector");

    std::cout << "PASS: test_get_by_category" << std::endl;
}

// ---------------------------------------------------------------------------
// test_data_access
// ---------------------------------------------------------------------------
void test_data_access() {
    auto eqs = make_test_equipment();
    EquipmentRegistry reg(eqs);

    const auto& data_map = reg.data();
    expect(data_map.size() == 3, "data() should have 3 entries");

    auto it = data_map.find(NSID("minecraft:diamond_sword"));
    expect(it != data_map.end(), "diamond_sword in data()");
    if (it != data_map.end()) {
        expect(it->second.name == "Diamond Sword", "data() value has correct name");
        expect(it->second.category == EquipmentTag::sword(), "data() value has correct category");
    }

    // Unknown name is not in the map
    expect(data_map.find(NSID("nonexistent")) == data_map.end(), "unknown name not in data()");

    std::cout << "PASS: test_data_access" << std::endl;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    try {
        test_initialize_and_get();
        test_get_bounds();
        test_get_by_category();
        test_data_access();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
