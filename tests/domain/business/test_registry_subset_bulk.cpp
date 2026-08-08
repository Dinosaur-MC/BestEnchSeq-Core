#define BESQ_TEST_MAIN
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/types/EquipmentTag.h"
#include "framework/test_framework.h"

#include <iostream>
#include <string>
#include <unordered_set>

// =============================================================================
// test_registry_subset_bulk.cpp
//
// Tests IRegistry<T>::create_subset() filtering (Section A) and large-scale
// bulk operations on EnchantmentRegistry (Section B).
// =============================================================================

namespace {

// ── Helper: bitwise platform check -------------------------------------------
// Returns true if e supports the given platform (including MCE::All).
bool supports_platform(const EnchInfo& e, MCE platform) {
    return (static_cast<int>(e.supported_platform) & static_cast<int>(platform)) != 0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Section A — Subset / Filter
// ═══════════════════════════════════════════════════════════════════════════════

// ── A1: Filter by max_level ------------------------------------------------
TEST_CASE("test_subset_by_max_level") {
    std::vector<EnchInfo> infos;
    infos.emplace_back(NSID("minecraft:ench_1"), "Ench 1", MCE::All, 1, 1, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});
    infos.emplace_back(NSID("minecraft:ench_2"), "Ench 2", MCE::All, 2, 2, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});
    infos.emplace_back(NSID("minecraft:ench_3"), "Ench 3", MCE::All, 3, 3, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});
    infos.emplace_back(NSID("minecraft:ench_4"), "Ench 4", MCE::All, 4, 4, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});

    EnchantmentRegistry reg(infos);
    auto subset = reg.create_subset([](const EnchInfo& e) { return e.max_level >= 3; });

    expect(subset.size() == 2, "subset(max_level >= 3) size == 2");
    expect(subset.contains(NSID("minecraft:ench_3")), "subset contains ench_3 (max_level=3)");
    expect(subset.contains(NSID("minecraft:ench_4")), "subset contains ench_4 (max_level=4)");
    expect(!subset.contains(NSID("minecraft:ench_1")), "subset excludes ench_1 (max_level=1)");
    expect(!subset.contains(NSID("minecraft:ench_2")), "subset excludes ench_2 (max_level=2)");
}

// ── A2: Filter by platform -------------------------------------------------
TEST_CASE("test_subset_by_platform") {
    std::vector<EnchInfo> infos;
    // Java-only
    infos.emplace_back(NSID("minecraft:ench_java"), "Java", MCE::Java, 5, 5, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});
    // Bedrock-only
    infos.emplace_back(NSID("minecraft:ench_bedrock"), "Bedrock", MCE::Bedrock, 5, 5, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});
    // Both platforms
    infos.emplace_back(NSID("minecraft:ench_all"), "All", MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});

    EnchantmentRegistry reg(infos);

    // Filter: anything supporting Java (Java-only or All)
    auto subset = reg.create_subset([](const EnchInfo& e) { return supports_platform(e, MCE::Java); });

    expect(subset.size() == 2, "subset(Java) size == 2");
    expect(subset.contains(NSID("minecraft:ench_java")), "subset contains Java-only ench");
    expect(subset.contains(NSID("minecraft:ench_all")), "subset contains All-platform ench");
    expect(!subset.contains(NSID("minecraft:ench_bedrock")), "subset excludes Bedrock-only ench");
}

// ── A3: Filter by applicable equipment -------------------------------------
TEST_CASE("test_subset_by_applicable") {
    std::vector<EnchInfo> infos;
    // Sharpness applies to swords
    infos.emplace_back(NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{EquipmentTag::sword()});
    // Protection applies to chestplates
    infos.emplace_back(NSID("minecraft:protection"), "Protection", MCE::All, 4, 4, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{EquipmentTag::chestplate()});
    // Unbreaking applies to both (or neither via empty set — just a control)
    infos.emplace_back(NSID("minecraft:unbreaking"), "Unbreaking", MCE::All, 3, 3, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});

    EnchantmentRegistry reg(infos);

    auto subset = reg.create_subset([](const EnchInfo& e) { return e.supported_items.contains(EquipmentTag::sword()); });

    expect(subset.size() == 1, "subset(sword-applicable) size == 1");
    expect(subset.contains(NSID("minecraft:sharpness")), "subset contains sharpness (applies to sword)");
    expect(!subset.contains(NSID("minecraft:protection")), "subset excludes protection (applies to chestplate)");
    expect(!subset.contains(NSID("minecraft:unbreaking")), "subset excludes unbreaking (no applicable equipments)");
}

// ── A4: Empty result -------------------------------------------------------
TEST_CASE("test_subset_empty_result") {
    std::vector<EnchInfo> infos;
    infos.emplace_back(NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});
    infos.emplace_back(NSID("minecraft:smite"), "Smite", MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});

    EnchantmentRegistry reg(infos);
    // Match nothing: max_level > 100
    auto subset = reg.create_subset([](const EnchInfo& e) { return e.max_level > 100; });

    expect(subset.size() == 0, "subset(match nothing) size == 0");
    expect(subset.empty(), "subset(match nothing) is empty");
}

// ── A5: All match ----------------------------------------------------------
TEST_CASE("test_subset_all_match") {
    std::vector<EnchInfo> infos;
    infos.emplace_back(NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});
    infos.emplace_back(NSID("minecraft:smite"), "Smite", MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});
    infos.emplace_back(NSID("minecraft:unbreaking"), "Unbreaking", MCE::All, 3, 3, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});

    EnchantmentRegistry reg(infos);
    // Match everything: max_level > 0
    auto subset = reg.create_subset([](const EnchInfo& e) { return e.max_level > 0; });

    expect(subset.size() == 3, "subset(match all) size == 3");
    expect(subset.contains(NSID("minecraft:sharpness")), "subset contains sharpness");
    expect(subset.contains(NSID("minecraft:smite")), "subset contains smite");
    expect(subset.contains(NSID("minecraft:unbreaking")), "subset contains unbreaking");
}

// ── A6: Subset on EquipmentRegistry by category ----------------------------
TEST_CASE("test_subset_equipment") {
    std::vector<Equipment> eqs;
    eqs.emplace_back(Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword", EquipmentTag::sword(), 1561});
    eqs.emplace_back(Equipment{NSID("minecraft:iron_sword"), "Iron Sword", EquipmentTag::sword(), 250});
    eqs.emplace_back(Equipment{NSID("minecraft:diamond_chestplate"), "Diamond Chestplate", EquipmentTag::chestplate(), 528});
    eqs.emplace_back(Equipment{NSID("minecraft:diamond_pickaxe"), "Diamond Pickaxe", EquipmentTag::pickaxe(), 1561});

    EquipmentRegistry reg(eqs);

    // Filter by sword category
    auto swords = reg.create_subset([](const Equipment& e) { return e.category == EquipmentTag::sword(); });

    expect(swords.size() == 2, "subset(sword category) size == 2");
    expect(swords.contains(NSID("minecraft:diamond_sword")), "subset has diamond_sword");
    expect(swords.contains(NSID("minecraft:iron_sword")), "subset has iron_sword");
    expect(!swords.contains(NSID("minecraft:diamond_chestplate")), "subset excludes chestplate");
    expect(!swords.contains(NSID("minecraft:diamond_pickaxe")), "subset excludes pickaxe");

    // Filter by pickaxe category
    auto pickaxes = reg.create_subset([](const Equipment& e) { return e.category == EquipmentTag::pickaxe(); });

    expect(pickaxes.size() == 1, "subset(pickaxe category) size == 1");
    expect(pickaxes.contains(NSID("minecraft:diamond_pickaxe")), "subset has diamond_pickaxe");

    // Filter by chestplate category
    auto chestplates = reg.create_subset([](const Equipment& e) { return e.category == EquipmentTag::chestplate(); });

    expect(chestplates.size() == 1, "subset(chestplate category) size == 1");
    expect(chestplates.contains(NSID("minecraft:diamond_chestplate")), "subset has diamond_chestplate");
}

// ── A7: Chained subsets ----------------------------------------------------
TEST_CASE("test_subset_chained") {
    std::vector<EnchInfo> infos;
    // max_level=5, Java
    infos.emplace_back(NSID("minecraft:ench_a"), "Ench A", MCE::Java, 5, 5, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});
    // max_level=5, Bedrock
    infos.emplace_back(NSID("minecraft:ench_b"), "Ench B", MCE::Bedrock, 5, 5, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});
    // max_level=2, Java
    infos.emplace_back(NSID("minecraft:ench_c"), "Ench C", MCE::Java, 2, 2, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});
    // max_level=2, Bedrock
    infos.emplace_back(NSID("minecraft:ench_d"), "Ench D", MCE::Bedrock, 2, 2, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});
    // max_level=5, All
    infos.emplace_back(NSID("minecraft:ench_e"), "Ench E", MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{},
                       std::unordered_set<NSID>{});

    EnchantmentRegistry reg(infos);

    // Subset 1: max_level >= 3  →  {ench_a, ench_b, ench_e}
    auto subset1 = reg.create_subset([](const EnchInfo& e) { return e.max_level >= 3; });

    expect(subset1.size() == 3, "chain: first subset (max_level>=3) size == 3");
    expect(subset1.contains(NSID("minecraft:ench_a")), "chain: first subset has ench_a");
    expect(subset1.contains(NSID("minecraft:ench_b")), "chain: first subset has ench_b");
    expect(subset1.contains(NSID("minecraft:ench_e")), "chain: first subset has ench_e");

    // Subset 2 on subset1: only Java-supporting → {ench_a, ench_e}
    auto subset2 = subset1.create_subset([](const EnchInfo& e) { return supports_platform(e, MCE::Java); });

    expect(subset2.size() == 2, "chain: second subset (Java) size == 2");
    expect(subset2.contains(NSID("minecraft:ench_a")), "chain: second subset has ench_a (Java+max>=3)");
    expect(subset2.contains(NSID("minecraft:ench_e")), "chain: second subset has ench_e (All+max>=3)");
    expect(!subset2.contains(NSID("minecraft:ench_b")), "chain: second subset excludes ench_b (Bedrock)");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Section B — Large-scale Bulk Operations
// ═══════════════════════════════════════════════════════════════════════════════

// ── B8: Bulk insert 200 ----------------------------------------------------
TEST_CASE("test_bulk_insert_200") {
    EnchantmentRegistry reg;

    for (int i = 0; i < 200; ++i) {
        std::string id_str = "minecraft:ench_" + std::to_string(i);
        auto result = reg.insert(EnchInfo{NSID(id_str), "Ench " + std::to_string(i), MCE::All, 5, 5, 1, false,
                                          std::unordered_set<NSID>{}, std::unordered_set<NSID>{}});
        if (!result.second) {
            throw test_error("bulk_insert_200: insert failed for " + id_str);
        }
    }

    expect(reg.size() == 200, "bulk insert 200: size == 200");

    // Check first, middle, last
    expect(reg.contains(NSID("minecraft:ench_0")), "bulk insert 200: contains ench_0");
    expect(reg.contains(NSID("minecraft:ench_99")), "bulk insert 200: contains ench_99");
    expect(reg.contains(NSID("minecraft:ench_199")), "bulk insert 200: contains ench_199");

    // Verify a few values via at()
    const auto& first = reg.at(NSID("minecraft:ench_0"));
    expect(first.name == "Ench 0", "bulk insert 200: ench_0 name");
    expect(first.max_level == 5, "bulk insert 200: ench_0 max_level");

    const auto& last = reg.at(NSID("minecraft:ench_199"));
    expect(last.name == "Ench 199", "bulk insert 200: ench_199 name");
    expect(last.max_level == 5, "bulk insert 200: ench_199 max_level");
}

// ── B9: Bulk insert 100, erase 30, verify ----------------------------------
TEST_CASE("test_bulk_insert_erase_mixed") {
    EnchantmentRegistry reg;

    // Insert 100 items
    for (int i = 0; i < 100; ++i) {
        std::string id_str = "minecraft:test_" + std::to_string(i);
        reg.insert(EnchInfo{NSID(id_str), "Test " + std::to_string(i), MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{},
                            std::unordered_set<NSID>{}});
    }
    expect(reg.size() == 100, "bulk erase: after insert size == 100");

    // Erase first 30 items (indices 0 through 29)
    for (int i = 0; i < 30; ++i) {
        bool erased = reg.erase(NSID("minecraft:test_" + std::to_string(i)));
        if (!erased) {
            throw test_error("bulk erase: erase returned false for test_" + std::to_string(i));
        }
    }

    expect(reg.size() == 70, "bulk erase: after erase size == 70");

    // Verify erased items are gone
    for (int i = 0; i < 30; ++i) {
        std::string id_str = "minecraft:test_" + std::to_string(i);
        expect(!reg.contains(NSID(id_str)), "bulk erase: erased test_" + std::to_string(i) + " not found");
    }

    // Verify remaining items still present
    for (int i = 30; i < 100; ++i) {
        std::string id_str = "minecraft:test_" + std::to_string(i);
        expect(reg.contains(NSID(id_str)), "bulk erase: remaining test_" + std::to_string(i) + " found");
    }
}

// ── B10: Bulk insert 100, update 20, verify --------------------------------
TEST_CASE("test_bulk_update") {
    EnchantmentRegistry reg;

    // Insert 100 items
    for (int i = 0; i < 100; ++i) {
        std::string id_str = "minecraft:upd_" + std::to_string(i);
        reg.insert(EnchInfo{NSID(id_str), "Upd " + std::to_string(i), MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{},
                            std::unordered_set<NSID>{}});
    }
    expect(reg.size() == 100, "bulk update: after insert size == 100");

    // Update first 20 items: change max_level to 10
    for (int i = 0; i < 20; ++i) {
        std::string id_str = "minecraft:upd_" + std::to_string(i);
        EnchInfo patch = reg.at(NSID(id_str));
        patch.max_level = 10;
        bool ok = reg.update(patch);
        if (!ok) {
            throw test_error("bulk update: update returned false for " + id_str);
        }
    }

    // Verify updated items
    for (int i = 0; i < 20; ++i) {
        std::string id_str = "minecraft:upd_" + std::to_string(i);
        const auto& ench = reg.at(NSID(id_str));
        expect(ench.max_level == 10, "bulk update: upd_" + std::to_string(i) + " max_level == 10");
        expect(ench.multiplier == 1, "bulk update: upd_" + std::to_string(i) + " multiplier unchanged");
    }

    // Verify unchanged items still have original values
    for (int i = 20; i < 100; ++i) {
        std::string id_str = "minecraft:upd_" + std::to_string(i);
        const auto& ench = reg.at(NSID(id_str));
        expect(ench.max_level == 5, "bulk update: upd_" + std::to_string(i) + " max_level == 5 (unchanged)");
        expect(ench.name == "Upd " + std::to_string(i), "bulk update: upd_" + std::to_string(i) + " name unchanged");
    }
}

} // anonymous namespace

// =============================================================================
// main
// =============================================================================
