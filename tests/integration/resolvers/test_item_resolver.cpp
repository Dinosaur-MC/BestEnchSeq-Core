#include "framework/test_utils.h"
#include "domain/algorithm/resolvers/ItemResolver.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentTagRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"

#include "domain/interface/cli/cli.h"
#include "domain/interface/parsers/EnchParser.h"

#include <iostream>
#include <stdexcept>

namespace {

struct TestRegistries {
    EquipmentTagRegistry cat_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;

    TestRegistries() {
        cat_reg = EquipmentTagRegistry({
            {EquipmentTag::sword(), "sword"},
        });

        eq_reg = EquipmentRegistry({Equipment{
            NSID("minecraft:diamond_sword"), "Diamond Sword",
            EquipmentTag::sword(), 1561
        }});

        std::vector<EnchInfo> infos;
        infos.push_back({NSID("minecraft:sharpness"), "Sharpness",
            MCE::All, 5, 5, 1, false, {},
            {EquipmentTag::sword()}});
        infos.push_back({NSID("minecraft:knockback"), "Knockback",
            MCE::All, 2, 2, 2, false, {},
            {EquipmentTag::sword()}});
        infos.push_back({NSID("minecraft:riptide"), "Riptide",
            MCE::All, 3, 3, 2, false,
            {NSID("minecraft:sharpness")},
            {NSID("#minecraft:trident")}});
        infos.push_back({NSID("minecraft:smite"), "Smite",
            MCE::All, 5, 5, 1, false,
            {NSID("minecraft:sharpness")},
            {EquipmentTag::sword()}});
        ench_reg = EnchantmentRegistry(infos);
    }
};

void test_resolve_basic() {
    TestRegistries regs;
    Item sword(regs.eq_reg.get("minecraft:diamond_sword"), {}, 0);
    EnchSet source;
    EnchSet target;
    target.emplace(static_cast<int32_t>(regs.ench_reg.index(NSID("minecraft:sharpness"))), 5);

    auto result = ItemResolver::resolve(sword, source, target, regs.ench_reg);
    expect(result.target_item.equipment.has_value(), "equipment preserved");
    expect(result.available_items.size() == 5,
           "sharpness 5 \xE2\x86\x92 5 graduated books");

    std::cout << "  PASS: test_resolve_basic" << std::endl;
}

void test_resolve_inapplicable_throws() {
    TestRegistries regs;
    Item sword(regs.eq_reg.get("minecraft:diamond_sword"), {}, 0);
    EnchSet target;
    target.emplace(static_cast<int32_t>(regs.ench_reg.index(NSID("minecraft:riptide"))), 1);

    bool threw = false;
    try {
        ItemResolver::resolve(sword, {}, target, regs.ench_reg);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "inapplicable enchantment should throw");

    std::cout << "  PASS: test_resolve_inapplicable_throws" << std::endl;
}

void test_resolve_conflict_throws() {
    TestRegistries regs;
    Item sword(regs.eq_reg.get("minecraft:diamond_sword"), {}, 0);
    EnchSet target;
    target.emplace(static_cast<int32_t>(regs.ench_reg.index(NSID("minecraft:sharpness"))), 5);
    target.emplace(static_cast<int32_t>(regs.ench_reg.index(NSID("minecraft:smite"))), 5);

    bool threw = false;
    try {
        ItemResolver::resolve(sword, {}, target, regs.ench_reg);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "conflicting enchantments should throw");

    std::cout << "  PASS: test_resolve_conflict_throws" << std::endl;
}

void test_resolve_diff_and_books() {
    TestRegistries regs;
    Item sword(regs.eq_reg.get("minecraft:diamond_sword"), {}, 0);
    EnchSet source;
    source.emplace(static_cast<int32_t>(regs.ench_reg.index(NSID("minecraft:sharpness"))), 3);
    EnchSet target;
    target.emplace(static_cast<int32_t>(regs.ench_reg.index(NSID("minecraft:sharpness"))), 5);
    target.emplace(static_cast<int32_t>(regs.ench_reg.index(NSID("minecraft:knockback"))), 2);

    auto result = ItemResolver::resolve(sword, source, target, regs.ench_reg);
    expect(result.available_items.size() == 4,
           "expected 4 books (sharp 4,5 + knock 1,2)");

    std::cout << "  PASS: test_resolve_diff_and_books" << std::endl;
}

void test_resolve_source_already_has_target() {
    TestRegistries regs;
    Item sword(regs.eq_reg.get("minecraft:diamond_sword"), {}, 0);
    EnchSet source;
    source.emplace(static_cast<int32_t>(regs.ench_reg.index(NSID("minecraft:sharpness"))), 5);
    EnchSet target;
    target.emplace(static_cast<int32_t>(regs.ench_reg.index(NSID("minecraft:sharpness"))), 5);

    auto result = ItemResolver::resolve(sword, source, target, regs.ench_reg);
    expect(result.available_items.empty(), "no books needed when source already meets target");

    std::cout << "  PASS: test_resolve_source_already_has_target" << std::endl;
}

void test_build_enchset_unknown_throws() {
    TestRegistries regs;
    auto specs = EnchParser::parse("sharpness=5,unknown_ench=1");
    bool threw = false;
    try {
        build_enchset(specs, regs.ench_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "build_enchset should throw on unknown enchantment");
    std::cout << "  PASS: test_build_enchset_unknown_throws" << std::endl;
}

// ---------------------------------------------------------------------------
// build_enchset success case
// ---------------------------------------------------------------------------
void test_build_enchset_success() {
    TestRegistries regs;
    auto specs = EnchParser::parse("sharpness=5");
    auto result = build_enchset(specs, regs.ench_reg);

    expect(result.size() == 1, "one enchantment resolved");
    if (result.size() == 1) {
        auto& ench = *result.begin();
        expect(ench.level == 5, "level should be 5");
    }
    std::cout << "  PASS: test_build_enchset_success" << std::endl;
}

// ---------------------------------------------------------------------------
// build_target success case
// ---------------------------------------------------------------------------
void test_build_target_success() {
    TestRegistries regs;
    TargetSpec spec;
    spec.item_id = "diamond_sword";
    spec.inline_enchants.push_back({"minecraft", "sharpness", 5});

    auto result = build_target(spec, regs.ench_reg, regs.eq_reg);
    expect(result.equipment.has_value(), "equipment should be set");
    expect(result.equipment->name_id == "minecraft:diamond_sword",
           "equipment should be diamond_sword");
    expect(result.enchantments.size() == 1, "one enchantment");
    std::cout << "  PASS: test_build_target_success" << std::endl;
}

// ---------------------------------------------------------------------------
// build_target throws on unknown equipment
// ---------------------------------------------------------------------------
void test_build_target_unknown_equip_throws() {
    TestRegistries regs;
    TargetSpec spec;
    spec.item_id = "nonexistent_sword";

    bool threw = false;
    try {
        build_target(spec, regs.ench_reg, regs.eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "build_target should throw on unknown equipment");
    std::cout << "  PASS: test_build_target_unknown_equip_throws" << std::endl;
}

} // anonymous namespace

int main() {
    std::cout << "=== ItemResolver Tests ===" << std::endl;
    try {
        test_resolve_basic();
        test_resolve_inapplicable_throws();
        test_resolve_conflict_throws();
        test_resolve_diff_and_books();
        test_resolve_source_already_has_target();
        test_build_enchset_unknown_throws();
        test_build_enchset_success();
        test_build_target_success();
        test_build_target_unknown_equip_throws();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
