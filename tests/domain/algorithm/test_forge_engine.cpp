#define BESQ_TEST_MAIN
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/types/Equipment.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/types/EquipmentTag.h"
#include "framework/test_framework.h"
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace {

// ─── Test fixtures ─────────────────────────────────────────────────

// Helper: build an EnchReg from the registry, mirroring the original
// vector-index approach but using registry's unordered_map iteration.
// The EnchReg is built with all enchantments (applicable flag is
// determined per original test).
struct TestFixture {
    EnchantmentRegistry enchants;
    algorithm::EnchReg reg;
    std::unordered_map<std::string, int16_t> name_to_local_id_;

    TestFixture() {
        std::vector<EnchInfo> infos;
        infos.push_back(
            {NSID("sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, std::unordered_set<NSID>{},
             std::unordered_set<NSID>{EquipmentTag::sword()}}
        );
        infos.push_back(
            {NSID("knockback"), "Knockback", MCE::All, 2, 2, 2, false, std::unordered_set<NSID>{},
             std::unordered_set<NSID>{EquipmentTag::sword()}}
        );
        infos.push_back(
            {NSID("bane_of_arthropods"), "Bane of Arthropods", MCE::All, 5, 5, 1, false,
             std::unordered_set<NSID>{NSID("sharpness")}, std::unordered_set<NSID>{EquipmentTag::sword()}}
        );
        infos.push_back(
            {NSID("protection"), "Protection", MCE::All, 4, 4, 1, false, std::unordered_set<NSID>{},
             std::unordered_set<NSID>{EquipmentTag::chestplate()}}
        );
        enchants = EnchantmentRegistry(infos);

        // Collect items in deterministic order (sorted by NSID id part) so
        // local IDs are stable across runs. NSID::str() returns "minecraft:xxx"
        // so extract just the id part for sorting and lookup.
        std::vector<std::pair<std::string, EnchInfo>> sorted;
        sorted.reserve(enchants.size());
        for (const auto &[nsid, info] : enchants.data()) {
            auto s = nsid.str();
            auto p = s.find(':');
            sorted.emplace_back(p != std::string::npos ? s.substr(p + 1) : s, info);
        }
        std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });

        // After sorting: 0=bane, 1=knockback, 2=protection, 3=sharpness
        for (int32_t i = 0; i < static_cast<int32_t>(sorted.size()); ++i)
            name_to_local_id_[sorted[i].first] = static_cast<int16_t>(i);

        algorithm::Equipment target_equip;
        target_equip.id             = "test";
        target_equip.max_durability = 1561;
        // Populate applicable_enchs BEFORE reg.init() — sorted is ready now.
        for (int32_t i = 0; i < static_cast<int32_t>(sorted.size()); ++i) {
            if (sorted[i].second.supported_items.count(EquipmentTag::sword()) > 0)
                target_equip.applicable_enchs.insert(static_cast<int16_t>(i));
        }

        std::vector<algorithm::EnchInfo> compact_infos;
        std::vector<NSID> global_ids;
        std::unordered_map<std::string, int32_t> name_to_local;

        for (int32_t i = 0; i < static_cast<int32_t>(sorted.size()); ++i) {
            global_ids.push_back(NSID(sorted[i].first));
            name_to_local[sorted[i].first] = i;
        }

        // Build exc_mask (ID-based): for each pair (i,j) where i's
        // exclusive_set contains j, set bit j in exc_masks[i] and vice versa.
        std::vector<algorithm::mask_type> exc_masks(sorted.size(), 0);
        for (int32_t i = 0; i < static_cast<int32_t>(sorted.size()); ++i) {
            for (int32_t j = i + 1; j < static_cast<int32_t>(sorted.size()); ++j) {
                auto id_i = NSID(sorted[i].first);
                auto id_j = NSID(sorted[j].first);
                bool conflict = sorted[i].second.exclusive_set.count(id_j) ||
                                sorted[j].second.exclusive_set.count(id_i);
                if (conflict) {
                    exc_masks[i] |= (algorithm::mask_type(1) << j);
                    exc_masks[j] |= (algorithm::mask_type(1) << i);
                }
            }
        }

        for (int32_t i = 0; i < static_cast<int32_t>(sorted.size()); ++i) {
            const auto &ei  = sorted[i].second;
            bool applicable = ei.supported_items.count(EquipmentTag::sword()) > 0;
            algorithm::EnchInfo info;
            info.id         = static_cast<uint8_t>(i);
            info.mul        = static_cast<uint8_t>(ei.multiplier);
            info.mul_b      = static_cast<uint8_t>(std::max(1, ei.multiplier >> 1));
            info.max_lvl    = static_cast<uint8_t>(ei.max_level);
            info.exc_mask   = exc_masks[i];
            info.applicable = applicable;
            compact_infos.push_back(std::move(info));
        }
        reg.init(compact_infos, global_ids, target_equip);
    }

    int16_t id(const std::string &name_id) const {
        auto it = name_to_local_id_.find(name_id);
        return it != name_to_local_id_.end() ? it->second : -1;
    }

    algorithm::Item make_book(int16_t ench_id, int16_t level) const {
        algorithm::Item book{algorithm::ItemType::Book, 0, 0, {}};
        book.enchs.insert(algorithm::Ench{static_cast<algorithm::Ench::value_type>(ench_id),
                                          static_cast<algorithm::Ench::value_type>(level)});
        return book;
    }

    algorithm::Item make_equip(int16_t ench_id, int16_t level) const {
        algorithm::Item eq{algorithm::ItemType::Equip, 1561, 0, {}};
        eq.enchs.insert(algorithm::Ench{static_cast<algorithm::Ench::value_type>(ench_id),
                                        static_cast<algorithm::Ench::value_type>(level)});
        return eq;
    }
};

// ─── Basic forge tests ─────────────────────────────────────────────

TEST_CASE("test_forge_books") {
    TestFixture fx;
    auto book_a = fx.make_book(fx.id("sharpness"), 4);
    auto book_b = fx.make_book(fx.id("sharpness"), 3);
    algorithm::ForgeEngine engine;
    auto [result, cost] = engine.forge(book_a, book_b, fx.reg);
    expect(result.type == algorithm::ItemType::Book, "result should be book");
    auto sid = static_cast<algorithm::Ench::value_type>(fx.id("sharpness"));
    expect(result.enchs.contains(sid) && result.enchs[sid] == 4, "book+book: should keep max level (4)");
    expect(cost == 4, "book+book cost should be 4");
    std::cout << "PASS: test_forge_books (cost=" << cost << ")" << std::endl;
}

TEST_CASE("test_forge_equipment_with_book") {
    TestFixture fx;
    auto eq   = algorithm::Item{algorithm::ItemType::Equip, 1561, 0, {}};
    auto book = fx.make_book(fx.id("sharpness"), 5);
    algorithm::ForgeEngine engine;
    auto [result, cost] = engine.forge(eq, book, fx.reg);
    expect(result.type == algorithm::ItemType::Equip, "result should be equipment");
    auto sid = static_cast<algorithm::Ench::value_type>(fx.id("sharpness"));
    expect(result.enchs.contains(sid) && result.enchs[sid] == 5, "result should have sharpness 5");
    expect(cost == 5, "forge cost for sharpness 5 to empty sword should be 5");
    std::cout << "PASS: test_forge_equipment_with_book (cost=" << cost << ")" << std::endl;
}

TEST_CASE("test_forge_incompatible_rejected") {
    TestFixture fx;
    algorithm::ForgeEngine engine;
    auto eq             = fx.make_equip(fx.id("sharpness"), 5);
    auto book           = fx.make_book(fx.id("bane_of_arthropods"), 4);
    auto [result, cost] = engine.forge(eq, book, fx.reg);
    {
        auto bid = static_cast<algorithm::Ench::value_type>(fx.id("bane_of_arthropods"));
        expect(!result.enchs.contains(bid), "incompatible enchant should not be applied");
    }
    {
        auto sid = static_cast<algorithm::Ench::value_type>(fx.id("sharpness"));
        expect(
            result.enchs.contains(sid) && result.enchs[sid] == 5,
            "non-conflicting sharpness 5 should be preserved after incompatible forge"
        );
    }
    expect(cost == 1, "incompatible penalty cost should be 1 (JE)");
    std::cout << "PASS: test_forge_incompatible_rejected (cost=" << cost << ")" << std::endl;
}

TEST_CASE("test_forge_not_forgeable") {
    TestFixture fx;
    algorithm::ForgeEngine engine;
    algorithm::Item mat{algorithm::ItemType::Material, 0, 0, {}};
    auto book = fx.make_book(fx.id("sharpness"), 1);
    expect(!engine.is_forgeable(mat, book), "material target should not be forgeable");
    std::cout << "PASS: test_forge_not_forgeable" << std::endl;
}

// ─── Sub-operation tests ──────────────────────────────────────────

TEST_CASE("test_penalty_cost") {
    algorithm::ForgeEngine engine;
    expect(engine.penalty_cost(0) == 0, "penalty_cost(0) should be 0");
    expect(engine.penalty_cost(1) == 1, "penalty_cost(1) should be 1");
    expect(engine.penalty_cost(2) == 3, "penalty_cost(2) should be 3");
    expect(engine.penalty_cost(3) == 7, "penalty_cost(3) should be 7");
    expect(engine.penalty_cost(4) == 15, "penalty_cost(4) should be 15");
    expect(engine.penalty_cost(5) == 31, "penalty_cost(5) should be 31");
    std::cout << "PASS: test_penalty_cost" << std::endl;
}

TEST_CASE("test_estimate_forge_cost") {
    TestFixture fx;
    algorithm::ForgeEngine engine;
    auto eq     = algorithm::Item{algorithm::ItemType::Equip, 1561, 0, {}};
    auto book   = fx.make_book(fx.id("sharpness"), 5);
    int32_t est = engine.estimate_forge_cost(eq, book, fx.reg);
    expect(est == 5, "estimate_forge_cost: equip+sharp5 should be 5");

    algorithm::Item eq_ppn{algorithm::ItemType::Equip, 1561, 2, {}};
    auto book2   = fx.make_book(fx.id("knockback"), 2);
    int32_t est2 = engine.estimate_forge_cost(eq_ppn, book2, fx.reg);
    expect(est2 == 5, "estimate_forge_cost: equip(ppn2)+knock2 should be 5");
    std::cout << "PASS: test_estimate_forge_cost" << std::endl;
}

// ─── BE platform tests ────────────────────────────────────────────

TEST_CASE("test_be_forge_cost") {
    TestFixture fx;
    algorithm::ForgeConfig be_cfg;
    be_cfg.ignore_penalty_cost = false;
    be_cfg.ignore_repair_cost  = false;
    be_cfg.platform            = MCE::Bedrock;
    algorithm::ForgeEngine be_engine{be_cfg};
    algorithm::ForgeConfig je_cfg;
    je_cfg.ignore_penalty_cost = false;
    je_cfg.ignore_repair_cost  = false;
    je_cfg.platform            = MCE::Java;
    algorithm::ForgeEngine je_engine{je_cfg};

    auto eq                   = fx.make_equip(fx.id("sharpness"), 3);
    auto book                 = fx.make_book(fx.id("sharpness"), 4);
    auto [be_result, be_cost] = be_engine.forge(eq, book, fx.reg);
    expect(be_cost == 1, "BE forge: sharpness 3+4 should cost 1");

    auto [je_result, je_cost] = je_engine.forge(eq, book, fx.reg);
    expect(je_cost == 4, "JE forge: sharpness 3+4 should cost 4");
    std::cout << "PASS: test_be_forge_cost (BE=" << be_cost << ", JE=" << je_cost << ")" << std::endl;
}

TEST_CASE("test_be_conflict_cost") {
    TestFixture fx;
    algorithm::ForgeConfig be_cfg2;
    be_cfg2.ignore_penalty_cost = false;
    be_cfg2.ignore_repair_cost  = false;
    be_cfg2.platform            = MCE::Bedrock;
    algorithm::ForgeEngine be_engine{be_cfg2};
    algorithm::ForgeConfig je_cfg2;
    je_cfg2.ignore_penalty_cost = false;
    je_cfg2.ignore_repair_cost  = false;
    je_cfg2.platform            = MCE::Java;
    algorithm::ForgeEngine je_engine{je_cfg2};

    auto eq                   = fx.make_equip(fx.id("sharpness"), 5);
    auto book                 = fx.make_book(fx.id("bane_of_arthropods"), 4);
    auto [be_result, be_cost] = be_engine.forge(eq, book, fx.reg);
    expect(be_cost == 0, "BE forge: conflict should cost 0");

    auto [je_result, je_cost] = je_engine.forge(eq, book, fx.reg);
    expect(je_cost == 1, "JE forge: conflict should cost 1");
    std::cout << "PASS: test_be_conflict_cost (BE=" << be_cost << ", JE=" << je_cost << ")" << std::endl;
}

// ─── forge_into mutation tests ────────────────────────────────────

TEST_CASE("test_ppn_recalculation") {
    TestFixture fx;
    algorithm::ForgeEngine engine;
    auto book_a         = fx.make_book(fx.id("sharpness"), 3);
    book_a.ppn          = 0;
    auto book_b         = fx.make_book(fx.id("knockback"), 2);
    book_b.ppn          = 2;
    auto [result, cost] = engine.forge(book_a, book_b, fx.reg);
    expect(result.ppn == 3, "PPN after forge(0, 2) should be 3");

    algorithm::Item equip{algorithm::ItemType::Equip, 1561, 3, {}};
    auto book_c = fx.make_book(fx.id("sharpness"), 1);
    book_c.ppn  = 0;
    (void)engine.forge_into(equip, book_c, fx.reg);
    expect(equip.ppn == 4, "PPN after equip(3)+book(0) should be 4");
    std::cout << "PASS: test_ppn_recalculation" << std::endl;
}

TEST_CASE("test_same_level_upgrade") {
    TestFixture fx;
    algorithm::ForgeEngine engine;
    auto book_a         = fx.make_book(fx.id("sharpness"), 4);
    auto book_b         = fx.make_book(fx.id("sharpness"), 4);
    auto [result, cost] = engine.forge(book_a, book_b, fx.reg);
    {
        auto sid = static_cast<algorithm::Ench::value_type>(fx.id("sharpness"));
        expect(
            result.enchs.contains(sid) && result.enchs[sid] == 5,
            "same level combine: 4+4 should become 5 (max_level of sharpness)"
        );
    }
    auto book_c           = fx.make_book(fx.id("sharpness"), 5);
    auto book_d           = fx.make_book(fx.id("sharpness"), 5);
    auto [result2, cost2] = engine.forge(book_c, book_d, fx.reg);
    {
        auto sid = static_cast<algorithm::Ench::value_type>(fx.id("sharpness"));
        expect(result2.enchs.contains(sid) && result2.enchs[sid] == 5, "max level combine: 5+5 should stay 5");
    }
    std::cout << "PASS: test_same_level_upgrade" << std::endl;
}

TEST_CASE("test_different_level_max") {
    TestFixture fx;
    algorithm::ForgeEngine engine;
    auto book_a         = fx.make_book(fx.id("sharpness"), 5);
    auto book_b         = fx.make_book(fx.id("sharpness"), 3);
    auto [result, cost] = engine.forge(book_a, book_b, fx.reg);
    {
        auto sid = static_cast<algorithm::Ench::value_type>(fx.id("sharpness"));
        expect(result.enchs.contains(sid) && result.enchs[sid] == 5, "different level combine: 5+3 should stay 5");
    }
    std::cout << "PASS: test_different_level_max" << std::endl;
}

// ─── Malformed item / error path tests ─────────────────────────────

TEST_CASE("test_invalid_enchant_level_rejected") {
    TestFixture fx;
    // level <= 0 is rejected by the new EnchSet (uint8_t storage)
    algorithm::Item book{algorithm::ItemType::Book, 0, 0, {}};
    bool inserted = book.enchs.insert(algorithm::Ench{static_cast<algorithm::Ench::value_type>(fx.id("sharpness")), 0});
    expect(!inserted, "zero-level enchant should be rejected by EnchSet");

    // Forging with an empty book leaves the equipment unchanged
    algorithm::ForgeEngine engine;
    auto eq             = fx.make_equip(fx.id("sharpness"), 3);
    auto [result, cost] = engine.forge(eq, book, fx.reg);
    {
        auto sid = static_cast<algorithm::Ench::value_type>(fx.id("sharpness"));
        expect(
            result.enchs.contains(sid) && result.enchs[sid] == 3,
            "book with rejected enchant should not reduce equipment's level"
        );
    }
    std::cout << "PASS: test_invalid_enchant_level_rejected (cost=" << cost << ")" << std::endl;
}

TEST_CASE("test_zero_level_enchant_rejected") {
    TestFixture fx;
    // make_book with level 0 → insert() returns false, book stays empty
    auto book = fx.make_book(fx.id("sharpness"), 0);
    expect(book.enchs.empty(), "zero-level enchant should not be stored in EnchSet");

    // Forge book (empty) onto equipment → equipment unchanged
    algorithm::ForgeEngine engine;
    auto eq             = fx.make_equip(fx.id("sharpness"), 3);
    auto [result, cost] = engine.forge(eq, book, fx.reg);
    {
        auto sid = static_cast<algorithm::Ench::value_type>(fx.id("sharpness"));
        expect(
            result.enchs.contains(sid) && result.enchs[sid] == 3,
            "book with zero-level enchant should not affect equipment"
        );
    }
    std::cout << "PASS: test_zero_level_enchant_rejected (cost=" << cost << ")" << std::endl;
}

// ─── Additional sub-operation boundary tests ─────────────────────────

TEST_CASE("test_penalty_cost_bounds") {
    algorithm::ForgeEngine engine;
    expect(engine.penalty_cost(-1) == INT32_MAX, "penalty_cost(-1) should be INT32_MAX");
    expect(engine.penalty_cost(31) == INT32_MAX, "penalty_cost(31) should be INT32_MAX");
    TEST_PASS("penalty_cost bounds");
}

TEST_CASE("test_is_forgeable_combinations") {
    TestFixture fx;
    algorithm::ForgeEngine engine;
    auto book        = fx.make_book(fx.id("sharpness"), 1);
    auto book2       = fx.make_book(fx.id("sharpness"), 1);
    algorithm::Item eq{algorithm::ItemType::Equip, 1561, 0, {}};
    algorithm::Item eq2{algorithm::ItemType::Equip, 1561, 0, {}};

    expect(engine.is_forgeable(eq, book), "equip + book forgeable");
    expect(engine.is_forgeable(eq, eq2), "equip + equip forgeable");
    expect(engine.is_forgeable(book, book2), "book + book forgeable");
    expect(!engine.is_forgeable(book, eq), "book + equip NOT forgeable");
    TEST_PASS("is_forgeable combinations");
}

TEST_CASE("test_forge_into_repair_cost") {
    algorithm::ForgeConfig cfg;
    cfg.ignore_penalty_cost = false;
    cfg.ignore_repair_cost  = false;
    cfg.platform            = MCE::Java;
    algorithm::ForgeEngine engine{cfg};

    // equip (damaged) + equip sacrifice → +2 repair cost + durability increase
    TestFixture fx;
    algorithm::Item target{algorithm::ItemType::Equip, 1561, 0, {}};
    target.dur = 800;
    algorithm::Item sacrifice{algorithm::ItemType::Equip, 1561, 0, {}};
    sacrifice.dur = 100;
    int32_t cost = engine.forge_into(target, sacrifice, fx.reg);
    expect(cost == 2, "equip+equip repair: cost should be 2 (0 penalty + 2 repair)");
    // 800 + 100 + 1561*12/100 = 800 + 100 + 187 = 1087
    expect(target.dur == 1087, "repair durability formula (target + sacrifice + 12% of max)");

    // ignore_repair_cost=true → no +2, no durability change
    algorithm::ForgeConfig cfg2 = cfg;
    cfg2.ignore_repair_cost = true;
    algorithm::ForgeEngine engine2{cfg2};
    algorithm::Item target2{algorithm::ItemType::Equip, 1561, 0, {}};
    target2.dur = 800;
    algorithm::Item sacrifice2{algorithm::ItemType::Equip, 1561, 0, {}};
    sacrifice2.dur = 100;
    int32_t cost2 = engine2.forge_into(target2, sacrifice2, fx.reg);
    expect(cost2 == 0, "ignore_repair_cost: cost should be 0");
    expect(target2.dur == 800, "ignore_repair_cost: durability unchanged");
    TEST_PASS("forge_into repair cost");
}

TEST_CASE("test_forge_into_inapplicable_ench_skipped") {
    TestFixture fx;
    algorithm::ForgeEngine engine;
    // protection is only applicable to chestplate — not to the sword target
    // (its applicable flag is false in the fixture).  Forging a protection
    // book onto the sword must skip it (no apply, no cost).
    algorithm::Item eq{algorithm::ItemType::Equip, 1561, 0, {}};
    auto book = fx.make_book(fx.id("protection"), 3);
    int32_t cost = engine.forge_into(eq, book, fx.reg);
    auto pid = static_cast<algorithm::Ench::value_type>(fx.id("protection"));
    expect(!eq.enchs.contains(pid), "inapplicable enchant should be skipped on equip");
    expect(cost == 0, "skipped enchant adds no cost");
    TEST_PASS("forge_into inapplicable enchant skip");
}

TEST_CASE("test_pure_forge_into") {
    TestFixture fx;
    algorithm::ForgeEngine engine;

    // book + book: same-level upgrade + ppn update (no cost arithmetic)
    auto a   = fx.make_book(fx.id("sharpness"), 4);
    a.ppn    = 0;
    auto b   = fx.make_book(fx.id("sharpness"), 4);
    b.ppn    = 1;
    engine.pure_forge_into(a, b, fx.reg);
    auto sid = static_cast<algorithm::Ench::value_type>(fx.id("sharpness"));
    expect(a.enchs.contains(sid) && a.enchs[sid] == 5,
           "pure_forge: 4+4 should upgrade to 5");
    expect(a.ppn == 2, "pure_forge: ppn(0,1) should become 2");

    // equip + equip: repair applied (durability increases), still cost-free
    algorithm::Item eq{algorithm::ItemType::Equip, 1561, 0, {}};
    eq.dur  = 800;
    algorithm::Item eq2{algorithm::ItemType::Equip, 1561, 0, {}};
    eq2.dur = 100;
    engine.pure_forge_into(eq, eq2, fx.reg);
    expect(eq.dur == 1087, "pure_forge: repair durability formula");

    TEST_PASS("pure_forge_into");
}

TEST_CASE("test_estimate_forge_cost_equip_sacrifice") {
    TestFixture fx;
    algorithm::ForgeEngine engine;
    // Equipment sacrifice uses mul (not mul_b): knockback mul=2.
    algorithm::Item eq{algorithm::ItemType::Equip, 1561, 0, {}};
    algorithm::Item sac{algorithm::ItemType::Equip, 1561, 0, {}};
    sac.enchs.insert(algorithm::Ench{
        static_cast<algorithm::Ench::value_type>(fx.id("knockback")), 2});
    int32_t est = engine.estimate_forge_cost(eq, sac, fx.reg);
    expect(est == 4, "estimate equip-sacrifice: knockback 2 × mul(2) = 4");

    // Book sacrifice uses mul_b (knockback mul_b = 1) — discriminates the path
    auto book      = fx.make_book(fx.id("knockback"), 2);
    int32_t est_bk = engine.estimate_forge_cost(eq, book, fx.reg);
    expect(est_bk == 2, "estimate book-sacrifice: knockback 2 × mul_b(1) = 2");
    TEST_PASS("estimate_forge_cost equipment sacrifice");
}

} // anonymous namespace
