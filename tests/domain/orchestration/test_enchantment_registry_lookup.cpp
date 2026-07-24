#include "framework/test_utils.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/EquipmentTag.h"

#include <iostream>

namespace {

void test_registry_contains_bare() {
    std::vector<EnchInfo> infos;
    infos.push_back({NSID("sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
                     std::unordered_set<NSID>{},
                     std::unordered_set<NSID>{EquipmentTag::sword()}});
    EnchantmentRegistry reg(infos);

    expect(reg.contains(NSID("sharpness")), "bare name 'sharpness' should be found");

    expect(!reg.contains(NSID("nonexistent")), "unknown name should not be found");

    std::cout << "  PASS: test_registry_contains_bare" << std::endl;
}

void test_registry_contains_namespaced() {
    std::vector<EnchInfo> infos;
    infos.push_back({NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
                     std::unordered_set<NSID>{},
                     std::unordered_set<NSID>{EquipmentTag::sword()}});
    EnchantmentRegistry reg(infos);

    // Bare lookup — NSID("sharpness") normalizes to "minecraft:sharpness"
    expect(reg.contains(NSID("sharpness")), "bare name should resolve to namespaced entry");

    // Full namespaced lookup
    expect(reg.contains(NSID("minecraft:sharpness")), "ns:id should resolve");

    // Unknown enchantment
    expect(!reg.contains(NSID("mod:unknown")), "unknown ns:id should not be found");

    std::cout << "  PASS: test_registry_contains_namespaced" << std::endl;
}

} // anonymous namespace

int main() {
    std::cout << "=== EnchantmentRegistry Lookup Tests ===" << std::endl;

    try {
        test_registry_contains_bare();
        test_registry_contains_namespaced();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
