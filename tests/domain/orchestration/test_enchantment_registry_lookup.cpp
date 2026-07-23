#include "framework/test_utils.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/EquipmentTag.h"

#include <iostream>

namespace {

void test_registry_get_id_bare() {
    EnchantmentRegistry reg;
    std::vector<EnchInfo> infos;
    infos.push_back({NSID("sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
                     std::unordered_set<NSID>{},
                     std::unordered_set<NSID>{EquipmentTag::sword()}});
    reg.initialize(infos);

    int32_t id = reg.get_id(NSID("sharpness"));
    expect(id >= 0, "bare name 'sharpness' should resolve");

    int32_t missing = reg.get_id(NSID("nonexistent"));
    expect(missing < 0, "unknown name should return -1");

    std::cout << "  PASS: test_registry_get_id_bare" << std::endl;
}

void test_registry_get_id_namespaced() {
    EnchantmentRegistry reg;
    std::vector<EnchInfo> infos;
    infos.push_back({NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
                     std::unordered_set<NSID>{},
                     std::unordered_set<NSID>{EquipmentTag::sword()}});
    reg.initialize(infos);

    // Bare lookup -- registry falls back to "minecraft:" prefix
    int32_t id = reg.get_id(NSID("sharpness"));
    expect(id >= 0, "bare name should resolve to namespaced entry");

    // Full namespaced lookup
    int32_t ns_id = reg.get_id(NSID("minecraft:sharpness"));
    expect(ns_id >= 0, "ns:id should resolve");

    // Unknown enchantment
    int32_t missing = reg.get_id(NSID("mod:unknown"));
    expect(missing < 0, "unknown ns:id should return -1");

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
