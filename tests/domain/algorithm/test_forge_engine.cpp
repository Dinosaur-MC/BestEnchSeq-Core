#include "framework/test_utils.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/business/registries/EquipmentCategoryRegistry.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/types/Equipment.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace {

// ─── Test fixtures ─────────────────────────────────────────────────

struct TestFixture {
    EnchantmentRegistry enchants;
    EquipmentCategoryRegistry categories;
    algorithm::EnchReg reg;

    TestFixture() {
        categories.initialize();

        // Enchantment data:
        //   sharpness (id=0), knockback (id=1), bane_of_arthropods (id=2), protection (id=3)
        std::vector<EnchInfo> infos;
        infos.push_back({"sharpness", "Sharpness", MCE::All, 5, 5,
                         1, false, {}, {EquipmentCategory::ID_SWORD}});
        infos.push_back({"knockback", "Knockback", MCE::All, 2, 2,
                         2, false, {}, {EquipmentCategory::ID_SWORD}});
        infos.push_back({"bane_of_arthropods", "Bane of Arthropods", MCE::All, 5, 5,
                         1, false, {"sharpness"}, {EquipmentCategory::ID_SWORD}});
        infos.push_back({"protection", "Protection", MCE::All, 4, 4,
                         1, false, {}, {EquipmentCategory::ID_CHESTPLATE}});
        enchants.initialize(infos);

        // Build compact EnchReg for sword
        algorithm::Equipment target_equip;
        target_equip.id = 0;
        target_equip.category_id = EquipmentCategory::ID_SWORD;
        target_equip.max_durability = 1561;
        std::vector<algorithm::EnchInfo> compact_infos;
        std::vector<int32_t> global_ids;
        // Map enchantment name → local id for conflict resolution
        std::unordered_map<std::string, int32_t> name_to_local;
        for (int32_t i = 0; i < static_cast<int32_t>(enchants.size()); ++i) {
            global_ids.push_back(i);
            name_to_local[enchants.get(i).name_id] = i;
        }
        size_t mask_size = (enchants.size() + 63) / 64;
        std::vector<std::vector<algorithm::MaskType>> exc_masks(enchants.size(),
            std::vector<algorithm::MaskType>(mask_size, 0));
        // Build conflict masks using shared group bits.
        // Each exclusive_set defines a conflict group.  All members share a
        // common bit position so is_conflict() (bitwise-AND) detects overlap.
        uint64_t next_group = 0;
        std::vector<bool> visited(enchants.size(), false);
        for (int32_t i = 0; i < static_cast<int32_t>(enchants.size()); ++i) {
            if (visited[i] || enchants.get(i).exclusive_set.empty()) continue;
            // Found a new conflict group — assign a shared bit to all members
            uint64_t group_bit = algorithm::MaskType(1) << (next_group % 64);
            next_group++;
            // Mark i and all its exclusive_set members with the same bit
            visited[i] = true;
            exc_masks[i][0] |= group_bit;
            for (const auto& ex_name : enchants.get(i).exclusive_set) {
                auto it = name_to_local.find(ex_name);
                if (it != name_to_local.end()) {
                    int32_t j = it->second;
                    visited[j] = true;
                    exc_masks[j][0] |= group_bit;
                }
            }
        }
        for (int32_t i = 0; i < static_cast<int32_t>(enchants.size()); ++i) {
            const auto& ei = enchants.get(i);
            bool applicable = ei.applicable_category_ids.count(EquipmentCategory::ID_SWORD) > 0;
            algorithm::EnchInfo info;
            info.mul = static_cast<uint16_t>(ei.multiplier);
            info.mul_b = static_cast<uint16_t>(ei.multiplier);
            info.max_lvl = static_cast<uint16_t>(ei.max_level);
            info.exc_mask = exc_masks[i];
            info.applicable = applicable;
            compact_infos.push_back(std::move(info));
        }
        reg.init(compact_infos, global_ids, target_equip);
    }

    // Get local IDs for named enchants
    int16_t id(const std::string& name_id) const {
        return static_cast<int16_t>(enchants.get_id(name_id));
    }

    algorithm::Item make_book(int16_t ench_id, int16_t level) const {
        algorithm::Item book{algorithm::ItemType::Book, 0, 0, {}};
        book.enchs.insert({ench_id, level});
        return book;
    }

    algorithm::Item make_equip(int16_t ench_id, int16_t level) const {
        algorithm::Item eq{algorithm::ItemType::Equip, 1561, 0, {}};
        eq.enchs.insert({ench_id, level});
        return eq;
    }
};

// ─── Basic forge tests ─────────────────────────────────────────────

void test_forge_books() {
    TestFixture fx;

    auto book_a = fx.make_book(fx.id("sharpness"), 4);
    auto book_b = fx.make_book(fx.id("sharpness"), 3);

    algorithm::ForgeEngine engine;
    auto [result, cost] = engine.forge(book_a, book_b, fx.reg);
    expect(result.type == algorithm::ItemType::Book, "result should be book");
    auto it = result.enchs.find(fx.id("sharpness"));
    expect(it != result.enchs.end() && it->level == 4,
           "book+book: should keep max level (4)");
    expect(cost == 4, "book+book cost should be 4");
    std::cout << "PASS: test_forge_books (cost=" << cost << ")" << std::endl;
}

void test_forge_equipment_with_book() {
    TestFixture fx;

    auto eq = algorithm::Item{algorithm::ItemType::Equip, 1561, 0, {}};
    auto book = fx.make_book(fx.id("sharpness"), 5);

    algorithm::ForgeEngine engine;
    auto [result, cost] = engine.forge(eq, book, fx.reg);
    expect(result.type == algorithm::ItemType::Equip, "result should be equipment");
    auto it = result.enchs.find(fx.id("sharpness"));
    expect(it != result.enchs.end() && it->level == 5, "result should have sharpness 5");
    expect(cost == 5, "forge cost for sharpness 5 to empty sword should be 5");
    std::cout << "PASS: test_forge_equipment_with_book (cost=" << cost << ")" << std::endl;
}

void test_forge_incompatible_rejected() {
    TestFixture fx;
    algorithm::ForgeEngine engine;

    auto eq = fx.make_equip(fx.id("sharpness"), 5);
    auto book = fx.make_book(fx.id("bane_of_arthropods"), 4);

    auto [result, cost] = engine.forge(eq, book, fx.reg);
    auto it = result.enchs.find(fx.id("bane_of_arthropods"));
    expect(it == result.enchs.end(), "incompatible enchant should not be applied");
    auto sharp_it = result.enchs.find(fx.id("sharpness"));
    expect(sharp_it != result.enchs.end() && sharp_it->level == 5,
           "non-conflicting sharpness 5 should be preserved after incompatible forge");
    expect(cost == 1, "incompatible penalty cost should be 1 (JE)");
    std::cout << "PASS: test_forge_incompatible_rejected (cost=" << cost << ")" << std::endl;
}

void test_forge_not_forgeable() {
    TestFixture fx;
    algorithm::ForgeEngine engine;

    algorithm::Item mat{algorithm::ItemType::Material, 0, 0, {}};
    auto book = fx.make_book(fx.id("sharpness"), 1);

    expect(!engine.is_forgeable(mat, book), "material target should not be forgeable");
    std::cout << "PASS: test_forge_not_forgeable" << std::endl;
}

// ─── Sub-operation tests ──────────────────────────────────────────

void test_penalty_cost() {
    algorithm::ForgeEngine engine;
    expect(engine.penalty_cost(0) == 0,  "penalty_cost(0) should be 0");
    expect(engine.penalty_cost(1) == 1,  "penalty_cost(1) should be 1");
    expect(engine.penalty_cost(2) == 3,  "penalty_cost(2) should be 3");
    expect(engine.penalty_cost(3) == 7,  "penalty_cost(3) should be 7");
    expect(engine.penalty_cost(4) == 15, "penalty_cost(4) should be 15");
    expect(engine.penalty_cost(5) == 31, "penalty_cost(5) should be 31");

    algorithm::ForgeConfig no_pen_cfg;
    no_pen_cfg.ignore_penalty_cost = true;
    no_pen_cfg.ignore_repair_cost = false;
    no_pen_cfg.ignore_cost_cap = false;
    no_pen_cfg.platform = MCE::Java;
    algorithm::ForgeEngine no_pen{no_pen_cfg};
    expect(no_pen.penalty_cost(5) == 0, "penalty_cost(5)+ignore should be 0");
    std::cout << "PASS: test_penalty_cost" << std::endl;
}

void test_apply_cap() {
    algorithm::ForgeEngine engine;
    expect(engine.apply_cap(0) == 0,  "apply_cap(0) should be 0");
    expect(engine.apply_cap(20) == 20, "apply_cap(20) should be 20");
    expect(engine.apply_cap(39) == 39, "apply_cap(39) should be 39");
    expect(engine.apply_cap(40) == 39, "apply_cap(40) should be 39");
    expect(engine.apply_cap(100) == 39, "apply_cap(100) should be 39");

    algorithm::ForgeConfig no_cap_cfg;
    no_cap_cfg.ignore_penalty_cost = false;
    no_cap_cfg.ignore_repair_cost = false;
    no_cap_cfg.ignore_cost_cap = true;
    no_cap_cfg.platform = MCE::Java;
    algorithm::ForgeEngine no_cap{no_cap_cfg};
    expect(no_cap.apply_cap(100) == 100, "apply_cap(100)+ignore should be 100");
    expect(no_cap.apply_cap(40) == 40,   "apply_cap(40)+ignore should be 40");
    std::cout << "PASS: test_apply_cap" << std::endl;
}

void test_estimate_forge_cost() {
    TestFixture fx;
    algorithm::ForgeEngine engine;

    auto eq = algorithm::Item{algorithm::ItemType::Equip, 1561, 0, {}};
    auto book = fx.make_book(fx.id("sharpness"), 5);
    int32_t est = engine.estimate_forge_cost(eq, book, fx.reg);
    expect(est == 5, "estimate_forge_cost: equip+sharp5 should be 5");

    algorithm::Item eq_ppn{algorithm::ItemType::Equip, 1561, 2, {}};
    auto book2 = fx.make_book(fx.id("knockback"), 2);
    int32_t est2 = engine.estimate_forge_cost(eq_ppn, book2, fx.reg);
    // penalty(2)=3 + penalty(0)=0 + knock2 * mul_b=2 = 3+4 = 7
    expect(est2 == 7, "estimate_forge_cost: equip(ppn2)+knock2 should be 7");
    std::cout << "PASS: test_estimate_forge_cost" << std::endl;
}

// ─── BE platform tests ────────────────────────────────────────────

void test_be_forge_cost() {
    TestFixture fx;

    algorithm::ForgeConfig be_cfg;
    be_cfg.ignore_penalty_cost = false;
    be_cfg.ignore_repair_cost = false;
    be_cfg.ignore_cost_cap = false;
    be_cfg.platform = MCE::Bedrock;
    algorithm::ForgeEngine be_engine{be_cfg};
    algorithm::ForgeConfig je_cfg;
    je_cfg.ignore_penalty_cost = false;
    je_cfg.ignore_repair_cost = false;
    je_cfg.ignore_cost_cap = false;
    je_cfg.platform = MCE::Java;
    algorithm::ForgeEngine je_engine{je_cfg};

    auto eq = fx.make_equip(fx.id("sharpness"), 3);
    auto book = fx.make_book(fx.id("sharpness"), 4);

    auto [be_result, be_cost] = be_engine.forge(eq, book, fx.reg);
    expect(be_cost == 1, "BE forge: sharpness 3+4 should cost 1");

    auto [je_result, je_cost] = je_engine.forge(eq, book, fx.reg);
    expect(je_cost == 4, "JE forge: sharpness 3+4 should cost 4");
    std::cout << "PASS: test_be_forge_cost (BE=" << be_cost << ", JE=" << je_cost << ")" << std::endl;
}

void test_be_conflict_cost() {
    TestFixture fx;

    algorithm::ForgeConfig be_cfg2;
    be_cfg2.ignore_penalty_cost = false;
    be_cfg2.ignore_repair_cost = false;
    be_cfg2.ignore_cost_cap = false;
    be_cfg2.platform = MCE::Bedrock;
    algorithm::ForgeEngine be_engine{be_cfg2};
    algorithm::ForgeConfig je_cfg2;
    je_cfg2.ignore_penalty_cost = false;
    je_cfg2.ignore_repair_cost = false;
    je_cfg2.ignore_cost_cap = false;
    je_cfg2.platform = MCE::Java;
    algorithm::ForgeEngine je_engine{je_cfg2};

    auto eq = fx.make_equip(fx.id("sharpness"), 5);
    auto book = fx.make_book(fx.id("bane_of_arthropods"), 4);

    auto [be_result, be_cost] = be_engine.forge(eq, book, fx.reg);
    expect(be_cost == 0, "BE forge: conflict should cost 0");

    auto [je_result, je_cost] = je_engine.forge(eq, book, fx.reg);
    expect(je_cost == 1, "JE forge: conflict should cost 1");
    std::cout << "PASS: test_be_conflict_cost (BE=" << be_cost << ", JE=" << je_cost << ")" << std::endl;
}

// ─── forge_into mutation tests ────────────────────────────────────

void test_ppn_recalculation() {
    TestFixture fx;
    algorithm::ForgeEngine engine;

    auto book_a = fx.make_book(fx.id("sharpness"), 3);
    book_a.ppn = 0;
    auto book_b = fx.make_book(fx.id("knockback"), 2);
    book_b.ppn = 2;

    auto [result, cost] = engine.forge(book_a, book_b, fx.reg);
    expect(result.ppn == 3, "PPN after forge(0, 2) should be 3");

    algorithm::Item equip{algorithm::ItemType::Equip, 1561, 3, {}};
    auto book_c = fx.make_book(fx.id("sharpness"), 1);
    book_c.ppn = 0;
    (void)engine.forge_into(equip, book_c, fx.reg);
    expect(equip.ppn == 4, "PPN after equip(3)+book(0) should be 4");

    std::cout << "PASS: test_ppn_recalculation (ppn1=" << static_cast<int>(result.ppn)
              << ", ppn2=" << static_cast<int>(equip.ppn) << ")" << std::endl;
}

void test_same_level_upgrade() {
    TestFixture fx;
    algorithm::ForgeEngine engine;

    auto book_a = fx.make_book(fx.id("sharpness"), 4);
    auto book_b = fx.make_book(fx.id("sharpness"), 4);
    auto [result, cost] = engine.forge(book_a, book_b, fx.reg);
    auto it = result.enchs.find(fx.id("sharpness"));
    expect(it != result.enchs.end() && it->level == 5,
           "same level combine: 4+4 should become 5 (max_level of sharpness)");

    auto book_c = fx.make_book(fx.id("sharpness"), 5);
    auto book_d = fx.make_book(fx.id("sharpness"), 5);
    auto [result2, cost2] = engine.forge(book_c, book_d, fx.reg);
    auto it2 = result2.enchs.find(fx.id("sharpness"));
    expect(it2 != result2.enchs.end() && it2->level == 5,
           "max level combine: 5+5 should stay 5");
    std::cout << "PASS: test_same_level_upgrade" << std::endl;
}

void test_different_level_max() {
    TestFixture fx;
    algorithm::ForgeEngine engine;

    auto book_a = fx.make_book(fx.id("sharpness"), 5);
    auto book_b = fx.make_book(fx.id("sharpness"), 3);
    auto [result, cost] = engine.forge(book_a, book_b, fx.reg);
    auto it = result.enchs.find(fx.id("sharpness"));
    expect(it != result.enchs.end() && it->level == 5,
           "different level combine: 5+3 should stay 5");
    std::cout << "PASS: test_different_level_max" << std::endl;
}

void test_cap_behavior() {
    TestFixture fx;
    TestFixture fx2;

    algorithm::ForgeEngine capped_engine;
    algorithm::ForgeConfig uncapped_cfg;
    uncapped_cfg.ignore_penalty_cost = false;
    uncapped_cfg.ignore_repair_cost = false;
    uncapped_cfg.ignore_cost_cap = true;
    uncapped_cfg.platform = MCE::Java;
    algorithm::ForgeEngine uncapped_engine{uncapped_cfg};

    algorithm::Item equip{algorithm::ItemType::Equip, 1561, 4, {}};
    algorithm::Item book{algorithm::ItemType::Book, 0, 4, {}};
    book.enchs.insert({fx.id("sharpness"), 5});
    book.enchs.insert({fx.id("knockback"), 2});

    // penalty(15)+penalty(15) + 5*1 + 2*2 = 30 + 5 + 4 = 39 (cap at 39)
    auto eq1 = equip;
    auto bk1 = book;
    int32_t capped_cost = capped_engine.forge_into(eq1, bk1, fx.reg);
    expect(capped_cost == 39, "capped cost should be 39");

    auto eq2 = equip;
    auto bk2 = book;
    int32_t uncapped_cost = uncapped_engine.forge_into(eq2, bk2, fx2.reg);
    expect(uncapped_cost == 39, "uncapped cost should also be 39");

    // High PPN to exceed cap
    algorithm::Item equip_high{algorithm::ItemType::Equip, 1561, 5, {}};
    algorithm::Item book_high{algorithm::ItemType::Book, 0, 5, {}};
    book_high.enchs.insert({fx.id("sharpness"), 5});

    auto eq3 = equip_high;
    auto bk3 = book_high;
    int32_t capped_high = capped_engine.forge_into(eq3, bk3, fx.reg);
    expect(capped_high == 39, "capped high cost should be 39");

    TestFixture fx3;
    auto eq4 = equip_high;
    auto bk4 = book_high;
    int32_t uncapped_high = uncapped_engine.forge_into(eq4, bk4, fx3.reg);
    expect(uncapped_high == 67, "uncapped high cost should be 67");

    std::cout << "PASS: test_cap_behavior (capped=" << capped_high
              << ", uncapped=" << uncapped_high << ")" << std::endl;
}

// ─── Malformed item / error path tests ─────────────────────────────

void test_negative_enchant_level() {
    TestFixture fx;
    algorithm::ForgeEngine engine;

    algorithm::Item book{algorithm::ItemType::Book, 0, 0, {}};
    book.enchs.insert({fx.id("sharpness"), -5});

    auto eq = fx.make_equip(fx.id("sharpness"), 3);
    auto [result, cost] = engine.forge(eq, book, fx.reg);
    auto it = result.enchs.find(fx.id("sharpness"));
    expect(it != result.enchs.end() && it->level == 3,
           "negative level book should not reduce equipment's enchantment level");
    std::cout << "PASS: test_negative_enchant_level (cost=" << cost << ")" << std::endl;
}

void test_zero_level_enchant() {
    TestFixture fx;
    algorithm::ForgeEngine engine;

    auto book = fx.make_book(fx.id("sharpness"), 0);
    auto eq = algorithm::Item{algorithm::ItemType::Equip, 1561, 0, {}};
    auto [result, cost] = engine.forge(eq, book, fx.reg);
    auto it = result.enchs.find(fx.id("sharpness"));
    expect(it != result.enchs.end() && it->level == 0,
           "zero-level enchant should be applied with level 0");
    std::cout << "PASS: test_zero_level_enchant (cost=" << cost << ")" << std::endl;
}

} // anonymous namespace

int main() {
    try {
        test_forge_books();
        test_forge_equipment_with_book();
        test_forge_incompatible_rejected();
        test_forge_not_forgeable();
        test_penalty_cost();
        test_apply_cap();
        test_estimate_forge_cost();
        test_be_forge_cost();
        test_be_conflict_cost();
        test_ppn_recalculation();
        test_same_level_upgrade();
        test_different_level_max();
        test_cap_behavior();
        test_negative_enchant_level();
        test_zero_level_enchant();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
