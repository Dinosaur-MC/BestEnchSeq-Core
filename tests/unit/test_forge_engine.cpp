#include "framework/test_utils.h"
#include "algorithm/forge/ForgeEngine.h"
#include "registries/CompactedRegistries.h"
#include "registries/RegistryAccess.h"
#include "registries/EquipmentCategoryRegistry.h"
#include "registries/EnchantmentRegistry.h"
#include "types/ForgeConfig.h"
#include <cstdint>
#include <vector>

namespace {

// ─── Helpers ───────────────────────────────────────────────────────────

// Named IDs resolved from the registry after setup_enchinfo() is called.
struct NamedIDs {
    int16_t sharpness;
    int16_t knockback;
    int16_t bane;
    int16_t protection;
};

NamedIDs get_ids() {
    NamedIDs ids;
    ids.sharpness  = static_cast<int16_t>(registries::enchants().get_id("sharpness"));
    ids.knockback  = static_cast<int16_t>(registries::enchants().get_id("knockback"));
    ids.bane       = static_cast<int16_t>(registries::enchants().get_id("bane_of_arthropods"));
    ids.protection = static_cast<int16_t>(registries::enchants().get_id("protection"));
    return ids;
}

void setup_enchinfo() {
    std::vector<EnchInfo> infos;
    infos.push_back({"sharpness", "Sharpness", MCE::All, 5, 5,
                     1, false, {}, {EquipmentCategory::ID_SWORD}});
    infos.push_back({"knockback", "Knockback", MCE::All, 2, 2,
                     2, false, {}, {EquipmentCategory::ID_SWORD}});
    infos.push_back({"bane_of_arthropods", "Bane of Arthropods", MCE::All, 5, 5,
                     1, false, {"sharpness"}, {EquipmentCategory::ID_SWORD}});
    infos.push_back({"protection", "Protection", MCE::All, 4, 4,
                     1, false, {}, {EquipmentCategory::ID_CHESTPLATE}});
    registries::enchants().initialize(infos);
    registries::categories().initialize();
}

Equipment sword{"diamond_sword", "Diamond Sword", EquipmentCategory::ID_SWORD, 1561};
Equipment chestplate{"diamond_chestplate", "Diamond Chestplate",
                      EquipmentCategory::ID_CHESTPLATE, 528};

compact::EnchReg init_reg() {
    compact::EnchReg reg;
    reg.init(registries::enchants(), sword);
    return reg;
}

compact::Item make_book(int16_t ench_id, int16_t level) {
    compact::Item book{compact::ItemType::Book, 0, 0, {}};
    book.enchs.insert({ench_id, level});
    return book;
}

compact::Item make_equip(int16_t ench_id, int16_t level) {
    compact::Item eq{compact::ItemType::Equip, 1561, 0, {}};
    eq.enchs.insert({ench_id, level});
    return eq;
}

// ─── Basic forge tests (refurbished with named IDs) ───────────────────

void test_forge_books() {
    setup_enchinfo();
    auto ids = get_ids();
    auto reg = init_reg();
    ForgeEngine engine;

    auto book_a = make_book(ids.sharpness, 4);
    auto book_b = make_book(ids.sharpness, 3);

    auto [result, cost] = engine.forge(book_a, book_b, reg);
    expect(result.type == compact::ItemType::Book, "result should be book");
    auto it = result.enchs.find(ids.sharpness);
    expect(it != result.enchs.end() && it->level == 4,
           "book+book: should keep max level (4)");
    // Book-to-book: penalty(0)+penalty(0) + mult(1)*new_level(4) = 4
    expect(cost == 4, "book+book cost should be 4");
    std::cout << "PASS: test_forge_books (cost=" << cost << ")" << std::endl;
}

void test_forge_equipment_with_book() {
    setup_enchinfo();
    auto ids = get_ids();
    auto reg = init_reg();
    ForgeEngine engine;

    auto eq = compact::Item{compact::ItemType::Equip, 1561, 0, {}};
    auto book = make_book(ids.sharpness, 5);

    auto [result, cost] = engine.forge(eq, book, reg);
    expect(result.type == compact::ItemType::Equip, "result should be equipment");
    auto it = result.enchs.find(ids.sharpness);
    expect(it != result.enchs.end() && it->level == 5, "result should have sharpness 5");
    // equip+book: penalty(0)+penalty(0) + mult(1)*5 = 5
    expect(cost == 5, "forge cost for sharpness 5 to empty sword should be 5");
    std::cout << "PASS: test_forge_equipment_with_book (cost=" << cost << ")" << std::endl;
}

void test_forge_incompatible_rejected() {
    setup_enchinfo();
    auto ids = get_ids();
    auto reg = init_reg();
    ForgeEngine engine;

    auto eq = make_equip(ids.sharpness, 5);
    auto book = make_book(ids.bane, 4);

    auto [result, cost] = engine.forge(eq, book, reg);
    auto it = result.enchs.find(ids.bane);
    expect(it == result.enchs.end(), "incompatible enchant should not be applied");
    auto sharp_it = result.enchs.find(ids.sharpness);
    expect(sharp_it != result.enchs.end() && sharp_it->level == 5,
           "non-conflicting sharpness 5 should be preserved after incompatible forge");
    expect(cost == 1, "incompatible penalty cost should be 1 (JE)");
    std::cout << "PASS: test_forge_incompatible_rejected (cost=" << cost << ")" << std::endl;
}

void test_forge_not_forgeable() {
    setup_enchinfo();
    auto ids = get_ids();
    auto reg = init_reg();
    ForgeEngine engine;

    compact::Item mat{compact::ItemType::Material, 0, 0, {}};
    auto book = make_book(ids.sharpness, 1);

    expect(!engine.is_forgeable(mat, book), "material target should not be forgeable");
    std::cout << "PASS: test_forge_not_forgeable" << std::endl;
}

// ─── Sub-operation tests ──────────────────────────────────────────────

void test_penalty_cost() {
    ForgeEngine engine;
    // PPN 0 → (1<<0)-1 = 0
    // PPN 1 → (1<<1)-1 = 1
    // PPN 2 → (1<<2)-1 = 3
    // PPN 3 → (1<<3)-1 = 7
    // PPN 4 → (1<<4)-1 = 15
    // PPN 5 → (1<<5)-1 = 31
    expect(engine.penalty_cost(0) == 0,  "penalty_cost(0) should be 0");
    expect(engine.penalty_cost(1) == 1,  "penalty_cost(1) should be 1");
    expect(engine.penalty_cost(2) == 3,  "penalty_cost(2) should be 3");
    expect(engine.penalty_cost(3) == 7,  "penalty_cost(3) should be 7");
    expect(engine.penalty_cost(4) == 15, "penalty_cost(4) should be 15");
    expect(engine.penalty_cost(5) == 31, "penalty_cost(5) should be 31");

    // ignore_penalty_cost → 0 for any PPN
    ForgeEngine no_pen{ForgeConfig{true, false, false, MCE::Java}};
    expect(no_pen.penalty_cost(5) == 0, "penalty_cost(5)+ignore should be 0");
    std::cout << "PASS: test_penalty_cost" << std::endl;
}

void test_apply_cap() {
    ForgeEngine engine;
    // raw < 39 → unchanged
    expect(engine.apply_cap(0) == 0,  "apply_cap(0) should be 0");
    expect(engine.apply_cap(20) == 20, "apply_cap(20) should be 20");
    // raw == 39 → unchanged
    expect(engine.apply_cap(39) == 39, "apply_cap(39) should be 39");
    // raw > 39 → capped to 39
    expect(engine.apply_cap(40) == 39, "apply_cap(40) should be 39");
    expect(engine.apply_cap(100) == 39, "apply_cap(100) should be 39");

    // ignore_cost_cap → raw value
    ForgeEngine no_cap{ForgeConfig{false, false, true, MCE::Java}};
    expect(no_cap.apply_cap(100) == 100, "apply_cap(100)+ignore should be 100");
    expect(no_cap.apply_cap(40) == 40,   "apply_cap(40)+ignore should be 40");
    std::cout << "PASS: test_apply_cap" << std::endl;
}

void test_estimate_forge_cost() {
    setup_enchinfo();
    auto ids = get_ids();
    auto reg = init_reg();
    ForgeEngine engine;

    // Equip + Book: sharpness 5 book to empty sword
    // est = penalty(0) + penalty(0) + 5*mult(1) = 5
    auto eq = compact::Item{compact::ItemType::Equip, 1561, 0, {}};
    auto book = make_book(ids.sharpness, 5);
    int32_t est = engine.estimate_forge_cost(eq, book, reg);
    expect(est == 5, "estimate_forge_cost: equip+sharp5 should be 5");

    // Equip(PPN 2) + Book: PPN 2 adds penalty_cost(2)=3
    compact::Item eq_ppn{compact::ItemType::Equip, 1561, 2, {}};
    auto book2 = make_book(ids.knockback, 2);
    // est = penalty(2) + penalty(0) + 2*bm(1) = 3 + 0 + 2 = 5
    int32_t est2 = engine.estimate_forge_cost(eq_ppn, book2, reg);
    expect(est2 == 5, "estimate_forge_cost: equip(ppn2)+knock2 should be 5");

    std::cout << "PASS: test_estimate_forge_cost" << std::endl;
}

// ─── BE platform tests ────────────────────────────────────────────────

void test_be_forge_cost() {
    setup_enchinfo();
    auto ids = get_ids();
    auto reg = init_reg();

    // BE: cost = mult * (new_level - old_level) for existing enchants
    ForgeEngine be_engine{ForgeConfig{false, false, false, MCE::Bedrock}};
    ForgeEngine je_engine{ForgeConfig{false, false, false, MCE::Java}};

    auto eq = make_equip(ids.sharpness, 3);  // already has sharpness 3
    auto book = make_book(ids.sharpness, 4);  // sacrifice is sharpness 4

    auto [be_result, be_cost] = be_engine.forge(eq, book, reg);
    // BE: mult(1) * (new_level(4) - old_level(3)) = 1, but it should
    // take max(3,4) = 4, so new_level = 4, old_level = 3
    // cost = 1 * (4 - 3) = 1
    // Plus penalty(0)+penalty(0) when not ignored
    // Total: penalty(0)+penalty(0) + 1 = 1
    // Wait: penalty in JE and BE is the same. PENALTY is not platform-dependent.
    // Let's look at the code:
    //   cost += penalty_cost(target.ppn) + penalty_cost(sacrifice.ppn)
    //   then for each enchant:
    //     if (plat == MCE::Java) cost += mult * new_level;
    //     else cost += mult * (new_level - old_level);
    // So penalty applies equally to both. BE just uses different cost formula.
    // penalty(0)+penalty(0) = 0. BE: mult(1) * (4-3) = 1. Total = 1.
    expect(be_cost == 1, "BE forge: sharpness 3+4 should cost 1");

    // JE: cost includes new_level * mult = 4*1 = 4
    // penalty(0)+penalty(0) = 0. JE: 4*1 = 4. Total = 4.
    auto [je_result, je_cost] = je_engine.forge(eq, book, reg);
    expect(je_cost == 4, "JE forge: sharpness 3+4 should cost 4");

    std::cout << "PASS: test_be_forge_cost (BE=" << be_cost << ", JE=" << je_cost << ")" << std::endl;
}

void test_be_conflict_cost() {
    setup_enchinfo();
    auto ids = get_ids();
    auto reg = init_reg();

    ForgeEngine be_engine{ForgeConfig{false, false, false, MCE::Bedrock}};
    ForgeEngine je_engine{ForgeConfig{false, false, false, MCE::Java}};

    auto eq = make_equip(ids.sharpness, 5);
    auto book = make_book(ids.bane, 4);

    // BE: conflict has no extra cost (BE doesn't have "+1 for conflict")
    auto [be_result, be_cost] = be_engine.forge(eq, book, reg);
    // In the code, conflict costs are:
    //   if (conflict) {
    //       if (plat == MCE::Java) cost += 1;
    //       continue;
    //   }
    // So BE: conflict → skip → cost unchanged (0 from enchants)
    // penalty(0)+penalty(0) = 0, no enchant cost → 0
    expect(be_cost == 0, "BE forge: conflict should cost 0");

    // JE: conflict → cost += 1
    auto [je_result, je_cost] = je_engine.forge(eq, book, reg);
    expect(je_cost == 1, "JE forge: conflict should cost 1");

    std::cout << "PASS: test_be_conflict_cost (BE=" << be_cost << ", JE=" << je_cost << ")" << std::endl;
}

// ─── forge_into mutation tests ────────────────────────────────────────

void test_ppn_recalculation() {
    setup_enchinfo();
    auto ids = get_ids();
    auto reg = init_reg();
    ForgeEngine engine;

    // Two books with different PPN: 0 and 2
    auto book_a = make_book(ids.sharpness, 3);
    book_a.ppn = 0;
    auto book_b = make_book(ids.knockback, 2);
    book_b.ppn = 2;

    auto [result, cost] = engine.forge(book_a, book_b, reg);
    // After forge: PPN = 1 + max(0, 2) = 3
    expect(result.ppn == 3, "PPN after forge(0, 2) should be 3");
    // PPN matched (book_a.ppn=0 < book_b.ppn=2, so take book_a's result.ppn=max+1=3)
    // Wait, the formula is: target.ppn = 1 + (target.ppn >= sacrifice.ppn ? target.ppn : sacrifice.ppn)
    // target=book_a(ppn=0), sacrifice=book_b(ppn=2)
    // 0 >= 2? No → sacrifice.ppn = 2. target.ppn = 1 + 2 = 3.
    // Actually, the forge_into mutates the first argument. Let's trace:
    // forge() copies book_a into result, then calls forge_into(result, book_b, reg)
    // result.ppn = 1 + (0 >= 2 ? 0 : 2) = 1 + 2 = 3 ✓

    // Forge equip(PPN 3) with book(PPN 0) → equip PPN should become 4 (3 >= 0)
    compact::Item equip{compact::ItemType::Equip, 1561, 3, {}};
    auto book_c = make_book(ids.sharpness, 1);
    book_c.ppn = 0;
    (void)engine.forge_into(equip, book_c, reg);
    expect(equip.ppn == 4, "PPN after equip(3)+book(0) should be 4");

    std::cout << "PASS: test_ppn_recalculation (ppn1=" << static_cast<int>(result.ppn)
              << ", ppn2=" << static_cast<int>(equip.ppn) << ")" << std::endl;
}

void test_same_level_upgrade() {
    setup_enchinfo();
    auto ids = get_ids();
    auto reg = init_reg();
    ForgeEngine engine;

    // Same level → level up: sharpness 4 + sharpness 4 → sharpness 5
    auto book_a = make_book(ids.sharpness, 4);
    auto book_b = make_book(ids.sharpness, 4);

    auto [result, cost] = engine.forge(book_a, book_b, reg);
    auto it = result.enchs.find(ids.sharpness);
    expect(it != result.enchs.end() && it->level == 5,
           "same level combine: 4+4 should become 5 (max_level of sharpness)");

    // Already at max: sharpness 5 + sharpness 5 → stays 5 (min(5+1, 5) = 5)
    auto book_c = make_book(ids.sharpness, 5);
    auto book_d = make_book(ids.sharpness, 5);
    auto [result2, cost2] = engine.forge(book_c, book_d, reg);
    auto it2 = result2.enchs.find(ids.sharpness);
    expect(it2 != result2.enchs.end() && it2->level == 5,
           "max level combine: 5+5 should stay 5");

    std::cout << "PASS: test_same_level_upgrade" << std::endl;
}

void test_different_level_max() {
    setup_enchinfo();
    auto ids = get_ids();
    auto reg = init_reg();
    ForgeEngine engine;

    // Different levels → take max: sharpness 5 + sharpness 3 → sharpness 5
    auto book_a = make_book(ids.sharpness, 5);
    auto book_b = make_book(ids.sharpness, 3);

    auto [result, cost] = engine.forge(book_a, book_b, reg);
    auto it = result.enchs.find(ids.sharpness);
    expect(it != result.enchs.end() && it->level == 5,
           "different level combine: 5+3 should stay 5");

    std::cout << "PASS: test_different_level_max" << std::endl;
}

void test_cap_behavior() {
    setup_enchinfo();
    auto ids = get_ids();
    auto reg = init_reg();
    auto reg2 = init_reg();

    ForgeEngine capped_engine;
    ForgeEngine uncapped_engine{ForgeConfig{false, false, true, MCE::Java}};

    // Build an expensive forge: many high-level enchants on a high-PPN item
    compact::Item equip{compact::ItemType::Equip, 1561, 4, {}};  // PPN 4 → penalty 15
    compact::Item book{compact::ItemType::Book, 0, 4, {}};       // PPN 4 → penalty 15
    book.enchs.insert({ids.sharpness, 5});  // mult 1 * 5 = 5
    book.enchs.insert({ids.knockback, 2});  // mult 2 * 2 = 4

    // penalty(15)+penalty(15) + bm(1)*5 + bm(1)*2 = 30 + 5 + 2 = 37
    auto eq1 = equip;
    auto bk1 = book;
    int32_t capped_cost = capped_engine.forge_into(eq1, bk1, reg);
    expect(capped_cost == 37, "capped cost should be 37");

    // Uncapped: same calculation, same result (under cap)
    auto eq2 = equip;
    auto bk2 = book;
    int32_t uncapped_cost = uncapped_engine.forge_into(eq2, bk2, reg2);
    expect(uncapped_cost == 37, "uncapped cost should also be 37");

    // Add more enchants to exceed cap
    book.enchs.insert({ids.protection, 4});  // Protection is chestplate-only...
    // Actually, protection won't apply to a sword. Let me use a different approach.
    // Just add knockback at max level - it has mult 2 so it adds more cost.
    // Actually the reg is initialized against sword, so protection can't apply to sword.
    // Let me just use multiple copies of the same enchants:
    // The forge_into logic only applies sacrifice enchants. So we can't "add more" books
    // in a single forge_into call. Instead, use high-PPN to increase cost:
    // PPN 5 → penalty 31, PPN 5 → penalty 31. Total penalty = 62, already over cap.
    compact::Item equip_high{compact::ItemType::Equip, 1561, 5, {}};
    compact::Item book_high{compact::ItemType::Book, 0, 5, {}};
    book_high.enchs.insert({ids.sharpness, 5});
    // uncapped: 31+31+5 = 67

    auto eq3 = equip_high;
    auto bk3 = book_high;
    int32_t capped_high = capped_engine.forge_into(eq3, bk3, reg);
    expect(capped_high == 39, "capped high cost should be 39");

    auto reg3 = init_reg();
    auto eq4 = equip_high;
    auto bk4 = book_high;
    int32_t uncapped_high = uncapped_engine.forge_into(eq4, bk4, reg3);
    expect(uncapped_high == 67, "uncapped high cost should be 67");

    std::cout << "PASS: test_cap_behavior (capped=" << capped_high
              << ", uncapped=" << uncapped_high << ")" << std::endl;
}

// ─── Malformed item / error path tests ─────────────────────────────────────

void test_negative_enchant_level() {
    setup_enchinfo();
    auto ids = get_ids();
    auto reg = init_reg();
    ForgeEngine engine;

    // Book with negative enchantment level -- should not crash
    compact::Item book{compact::ItemType::Book, 0, 0, {}};
    book.enchs.insert({ids.sharpness, -5});

    auto eq = make_equip(ids.sharpness, 3);

    auto [result, cost] = engine.forge(eq, book, reg);
    auto it = result.enchs.find(ids.sharpness);
    // std::max(3, -5) = 3, so existing level should be preserved
    expect(it != result.enchs.end() && it->level == 3,
           "negative level book should not reduce equipment's enchantment level");
    std::cout << "PASS: test_negative_enchant_level (cost=" << cost << ")" << std::endl;
}

void test_zero_level_enchant() {
    setup_enchinfo();
    auto ids = get_ids();
    auto reg = init_reg();
    ForgeEngine engine;

    // Enchant with level 0 -- verify it is accepted and doesn't crash
    auto book = make_book(ids.sharpness, 0);
    auto eq = compact::Item{compact::ItemType::Equip, 1561, 0, {}};

    auto [result, cost] = engine.forge(eq, book, reg);
    auto it = result.enchs.find(ids.sharpness);
    expect(it != result.enchs.end() && it->level == 0,
           "zero-level enchant should be applied with level 0");
    std::cout << "PASS: test_zero_level_enchant (cost=" << cost << ")" << std::endl;
}

} // anonymous namespace

int main() {
    try {
        // Basic forge
        test_forge_books();
        test_forge_equipment_with_book();
        test_forge_incompatible_rejected();
        test_forge_not_forgeable();

        // Sub-operations
        test_penalty_cost();
        test_apply_cap();
        test_estimate_forge_cost();

        // BE platform
        test_be_forge_cost();
        test_be_conflict_cost();

        // Mutation / PPN / level combine
        test_ppn_recalculation();
        test_same_level_upgrade();
        test_different_level_max();

        // Cap behavior
        test_cap_behavior();

        // Malformed item error paths
        test_negative_enchant_level();
        test_zero_level_enchant();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
