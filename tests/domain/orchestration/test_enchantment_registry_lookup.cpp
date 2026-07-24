#include "framework/test_utils.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/EquipmentTag.h"

#include <iostream>

namespace {

void test_registry_get_id_bare() {
    std::vector<EnchInfo> infos;
    infos.push_back({NSID("sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
                     std::unordered_set<NSID>{},
                     std::unordered_set<NSID>{EquipmentTag::sword()}});
    EnchantmentRegistry reg(infos);

    size_t id = reg.index(NSID("sharpness"));
    expect(id != EnchantmentRegistry::nops, "bare name 'sharpness' should resolve");

    size_t missing = reg.index(NSID("nonexistent"));
    expect(missing == EnchantmentRegistry::nops, "unknown name should return nops");

    std::cout << "  PASS: test_registry_get_id_bare" << std::endl;
}

void test_registry_get_id_namespaced() {
    std::vector<EnchInfo> infos;
    infos.push_back({NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
                     std::unordered_set<NSID>{},
                     std::unordered_set<NSID>{EquipmentTag::sword()}});
    EnchantmentRegistry reg(infos);

    // Bare lookup -- NSID("sharpness") normalizes to "minecraft:sharpness"
    size_t id = reg.index(NSID("sharpness"));
    expect(id != EnchantmentRegistry::nops, "bare name should resolve to namespaced entry");

    // Full namespaced lookup
    size_t ns_id = reg.index(NSID("minecraft:sharpness"));
    expect(ns_id != EnchantmentRegistry::nops, "ns:id should resolve");

    // Unknown enchantment
    size_t missing = reg.index(NSID("mod:unknown"));
    expect(missing == EnchantmentRegistry::nops, "unknown ns:id should return nops");

    std::cout << "  PASS: test_registry_get_id_namespaced" << std::endl;
}

} // anonymous namespace

int main() {
    std::cout << "=== EnchantmentRegistry Lookup Tests ===" << std::endl;

    try {
        test_registry_get_id_bare();
        test_registry_get_id_namespaced();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
