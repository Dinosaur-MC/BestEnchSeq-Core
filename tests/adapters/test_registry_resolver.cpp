#include "framework/test_utils.h"
#include "adapters/RegistryResolver.h"
#include "registries/EnchantmentRegistry.h"
#include "registries/RegistryAccess.h"

#include <iostream>

namespace {

void test_resolve_ench_id_bare_name() {
    EnchantmentRegistry reg;
    std::vector<EnchInfo> infos;
    infos.push_back({"sharpness", "Sharpness", MCE::All, 5, 5, 1, {},
                     {EquipmentCategory::ID_SWORD}});
    reg.initialize(infos);

    int32_t id = RegistryResolver::resolve_ench_id("sharpness", reg);
    expect(id >= 0, "bare name 'sharpness' should resolve");

    int32_t missing = RegistryResolver::resolve_ench_id("nonexistent", reg);
    expect(missing < 0, "unknown name should return -1");

    std::cout << "  [OK] test_resolve_ench_id_bare_name" << std::endl;
}

void test_resolve_ench_id_namespaced() {
    EnchantmentRegistry reg;
    std::vector<EnchInfo> infos;
    infos.push_back({"minecraft:sharpness", "Sharpness", MCE::All, 5, 5, 1, {},
                     {EquipmentCategory::ID_SWORD}});
    reg.initialize(infos);

    // Bare lookup should find it (registry tries bare id first)
    int32_t id = RegistryResolver::resolve_ench_id("sharpness", reg);
    expect(id >= 0, "bare name should resolve to namespaced entry");

    // Namespace + id resolution
    int32_t ns_id = RegistryResolver::resolve_ench_id("minecraft", "sharpness", reg);
    expect(ns_id >= 0, "ns:id should resolve");

    // Unknown namespace
    bool threw = false;
    try {
        RegistryResolver::resolve_ench_id("mod", "unknown", reg);
    } catch (const std::runtime_error &) {
        threw = true;
    }
    expect(threw, "unknown ns:id should throw");

    std::cout << "  [OK] test_resolve_ench_id_namespaced" << std::endl;
}

} // anonymous namespace

int main() {
    std::cout << "=== RegistryResolver Tests ===" << std::endl;

    try {
        test_resolve_ench_id_bare_name();
        test_resolve_ench_id_namespaced();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
