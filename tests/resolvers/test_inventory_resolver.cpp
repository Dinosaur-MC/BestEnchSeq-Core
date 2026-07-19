#include "framework/test_utils.h"
#include "resolvers/InventoryResolver.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EquipmentRegistry.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

struct TestEnv {
    EquipmentCategoryRegistry cat_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    std::filesystem::path temp_dir;

    TestEnv() {
        cat_reg.initialize();

        eq_reg.initialize({Equipment{
            "minecraft:diamond_sword", "Diamond Sword",
            EquipmentCategory::ID_SWORD, 1561
        }});

        std::vector<EnchInfo> infos;
        infos.push_back({"minecraft:sharpness", "Sharpness",
            MCE::All, 5, 5, 1, false, {},
            {EquipmentCategory::ID_SWORD}});
        ench_reg.initialize(infos);

        temp_dir = std::filesystem::temp_directory_path() / "besq_test_inv";
        std::filesystem::create_directories(temp_dir);
    }

    ~TestEnv() { std::filesystem::remove_all(temp_dir); }

    std::string write_inv(const std::string& json_content) {
        auto p = temp_dir / "inv.json";
        std::ofstream f(p);
        f << json_content;
        return p.string();
    }
};

void test_parse_book_only() {
    TestEnv env;
    auto path = env.write_inv(R"({"items":[
        {"type":"book","enchants":[{"id":"sharpness","level":5}],"prior_penalty":0}
    ]})");
    auto result = InventoryResolver::resolve(path, env.ench_reg, env.eq_reg);
    expect(result.items.size() == 1, "one book parsed");
    expect(result.warnings.empty(), "no warnings");
    std::cout << "  PASS: test_parse_book_only" << std::endl;
}

void test_parse_equipment() {
    TestEnv env;
    auto path = env.write_inv(R"({"items":[
        {"type":"equipment","id":"diamond_sword","enchants":[],"prior_penalty":0,"durability":1561}
    ]})");
    auto result = InventoryResolver::resolve(path, env.ench_reg, env.eq_reg);
    expect(result.items.size() == 1, "one equipment parsed");
    expect(result.warnings.empty(), "no warnings");
    std::cout << "  PASS: test_parse_equipment" << std::endl;
}

void test_unknown_enchant_warns() {
    TestEnv env;
    auto path = env.write_inv(R"({"items":[
        {"type":"book","enchants":[{"id":"nonexistent","level":1}],"prior_penalty":0}
    ]})");
    auto result = InventoryResolver::resolve(path, env.ench_reg, env.eq_reg);
    expect(!result.warnings.empty(), "warning for unknown enchant");
    std::cout << "  PASS: test_unknown_enchant_warns" << std::endl;
}

void test_empty_inventory() {
    TestEnv env;
    auto path = env.write_inv(R"({"items":[]})");
    auto result = InventoryResolver::resolve(path, env.ench_reg, env.eq_reg);
    expect(result.items.empty(), "empty items");
    std::cout << "  PASS: test_empty_inventory" << std::endl;
}

void test_unknown_equipment_warns() {
    TestEnv env;
    auto path = env.write_inv(R"({"items":[
        {"type":"equipment","id":"nonexistent_sword","enchants":[],"prior_penalty":0}
    ]})");
    auto result = InventoryResolver::resolve(path, env.ench_reg, env.eq_reg);
    expect(result.items.size() == 1, "unknown equipment treated as book");
    expect(!result.warnings.empty(), "warning for unknown equipment");
    std::cout << "  PASS: test_unknown_equipment_warns" << std::endl;
}

void test_sort_by_priority() {
    TestEnv env;
    auto path = env.write_inv(R"({"items":[
        {"type":"book","enchants":[],"prior_penalty":0,"priority":99},
        {"type":"book","enchants":[],"prior_penalty":0,"priority":1},
        {"type":"book","enchants":[],"prior_penalty":0,"priority":50}
    ]})");
    auto result = InventoryResolver::resolve(path, env.ench_reg, env.eq_reg);
    expect(result.items.size() == 3, "three books parsed");
    expect(result.items[0].priority == 1, "first item has lowest priority value");
    expect(result.items[1].priority == 50, "second item has middle priority value");
    expect(result.items[2].priority == 99, "third item has highest priority value");
    std::cout << "  PASS: test_sort_by_priority" << std::endl;
}

void test_missing_type_field() {
    TestEnv env;
    auto path = env.write_inv(R"({"items":[
        {"enchants":[],"prior_penalty":0}
    ]})");
    auto result = InventoryResolver::resolve(path, env.ench_reg, env.eq_reg);
    expect(result.items.empty(), "no items parsed from missing type");
    expect(!result.warnings.empty(), "warning for missing type field");
    std::cout << "  PASS: test_missing_type_field" << std::endl;
}

void test_default_priority() {
    TestEnv env;
    auto path = env.write_inv(R"({"items":[
        {"type":"book","enchants":[],"prior_penalty":0}
    ]})");
    auto result = InventoryResolver::resolve(path, env.ench_reg, env.eq_reg);
    expect(result.items.size() == 1, "one book parsed without priority");
    expect(result.items[0].priority == 99, "default priority is 99");
    std::cout << "  PASS: test_default_priority" << std::endl;
}

} // anonymous namespace

int main() {
    std::cout << "=== InventoryResolver Tests ===" << std::endl;
    try {
        test_parse_book_only();
        test_parse_equipment();
        test_unknown_enchant_warns();
        test_empty_inventory();
        test_unknown_equipment_warns();
        test_sort_by_priority();
        test_missing_type_field();
        test_default_priority();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
