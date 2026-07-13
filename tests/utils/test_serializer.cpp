#include "framework/test_utils.h"
#include "adapters/Serializer.hpp"
#include "registries/EnchantmentRegistry.h"
#include "registries/RegistryAccess.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EquipmentRegistry.h"
#include "config/ForgeConfig.h"

namespace {

void setup() {
    // Minimal enchantment registry for EnchSet tests
    std::vector<EnchInfo> infos;
    infos.push_back({"sharpness", "Sharpness", MCE::All, 5, 5, 1, false, {}, {}});
    infos.push_back({"knockback", "Knockback", MCE::All, 2, 2, 2, false, {}, {}});
    infos.push_back({"fire_aspect", "Fire Aspect", MCE::All, 2, 2, 2, false, {}, {}});
    registries::enchants().initialize(infos);

    // Equipment registry for ItemStack tests
    Equipment sword{"diamond_sword", "Diamond Sword", EquipmentCategory::ID_SWORD, 1561};
    Equipment boots{"diamond_boots", "Diamond Boots", EquipmentCategory::ID_BOOTS, 433};
    registries::equipment().initialize({sword, boots});
}

// ─── Primitive round-trips ───

void test_primitive_u32() {
    Serializer s;
    s.u32(42);
    s.u32(0);
    s.u32(0xFFFFFFFF);

    Deserializer d(s.data());
    expect(d.u32() == 42, "u32 round-trip: 42");
    expect(d.u32() == 0, "u32 round-trip: 0");
    expect(d.u32() == 0xFFFFFFFF, "u32 round-trip: max");
    expect(d.ok(), "deserializer should still be ok");
    std::cout << "PASS: test_primitive_u32" << std::endl;
}

void test_primitive_i32() {
    Serializer s;
    s.i32(-1);
    s.i32(0);
    s.i32(INT32_MAX);
    s.i32(INT32_MIN);

    Deserializer d(s.data());
    expect(d.i32() == -1, "i32 round-trip: -1");
    expect(d.i32() == 0, "i32 round-trip: 0");
    expect(d.i32() == INT32_MAX, "i32 round-trip: max");
    expect(d.i32() == INT32_MIN, "i32 round-trip: min");
    std::cout << "PASS: test_primitive_i32" << std::endl;
}

void test_primitive_string() {
    Serializer s;
    s.string("hello");
    s.string("");
    s.string(std::string(256, 'x'));

    Deserializer d(s.data());
    expect(d.string() == "hello", "string round-trip: hello");
    expect(d.string() == "", "string round-trip: empty");
    expect(d.string() == std::string(256, 'x'), "string round-trip: 256 chars");
    std::cout << "PASS: test_primitive_string" << std::endl;
}

// ─── Ench round-trip ───

void test_ench_roundtrip() {
    setup();

    Serializer s;
    s.write(Ench(0, 5));
    s.write(Ench(1, 2));

    Deserializer d(s.data());
    Ench e1 = d.read_ench();
    Ench e2 = d.read_ench();
    expect(e1.id == 0 && e1.level == 5, "ench round-trip: sharpness 5");
    expect(e2.id == 1 && e2.level == 2, "ench round-trip: knockback 2");
    expect(d.ok(), "deserializer ok after ench reads");
    std::cout << "PASS: test_ench_roundtrip" << std::endl;
}

// ─── EnchSet round-trip ───

void test_enchset_roundtrip() {
    setup();

    EnchSet set;
    set.insert(Ench(1, 2));
    set.insert(Ench(0, 5));

    Serializer s;
    s.write(set);

    Deserializer d(s.data());
    EnchSet result = d.read_ench_set();
    expect(result.size() == 2, "enchset should have 2 elements");
    expect(result.find(Ench(0, 5)) != result.end(), "enchset should contain sharpness 5");
    expect(result.find(Ench(1, 2)) != result.end(), "enchset should contain knockback 2");
    expect(d.ok(), "deserializer ok after enchset read");
    std::cout << "PASS: test_enchset_roundtrip" << std::endl;
}

// ─── ItemStack round-trip ───

void test_itemstack_roundtrip_book() {
    setup();

    EnchSet ench;
    ench.insert(Ench(0, 5));
    ItemStack book(ench, 0);  // enchanted book

    Serializer s;
    s.write(book);

    Deserializer d(s.data());
    ItemStack result = d.read_item_stack();
    expect(result.is_book(), "round-tripped book should be a book");
    expect(result.enchantments.find(Ench(0, 5)) != result.enchantments.end(),
           "round-tripped book should have sharpness 5");
    expect(d.ok(), "deserializer ok after itemstack read");
    std::cout << "PASS: test_itemstack_roundtrip_book" << std::endl;
}

void test_itemstack_roundtrip_equipment() {
    setup();

    auto& eq = registries::equipment().get("diamond_sword");

    EnchSet ench;
    ench.insert(Ench(0, 5));
    ItemStack item(eq, ench, 2, 1000);

    Serializer s;
    s.write(item);

    Deserializer d(s.data());
    ItemStack result = d.read_item_stack();
    expect(!result.is_book(), "round-tripped equipment should not be a book");
    expect(result.equipment.has_value(), "equipment pointer should not be null");
    expect(result.equipment->name_id == "diamond_sword",
           "equipment id should be diamond_sword, got: " + result.equipment->name_id);
    expect(result.prior_penalty == 2, "prior_penalty should be 2");
    expect(result.durability == 1000, "durability should be 1000");
    expect(result.enchantments.find(Ench(0, 5)) != result.enchantments.end(),
           "should have sharpness 5");
    expect(d.ok(), "deserializer ok after equipment read");
    std::cout << "PASS: test_itemstack_roundtrip_equipment" << std::endl;
}

} // namespace

int main() {
    std::cout << "=== Serializer Tests ===" << std::endl;
    try {
        test_primitive_u32();
        test_primitive_i32();
        test_primitive_string();
        test_ench_roundtrip();
        test_enchset_roundtrip();
        test_itemstack_roundtrip_book();
        test_itemstack_roundtrip_equipment();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}

