// =============================================================================
// Search component tests — Heuristic / HeuristicBasic / ItemPool / SearchUtils.
//
// These are the building blocks shared by the pool-based search strategies
// (A*, IDA*); they had no direct unit tests (only indirect coverage through
// end-to-end strategy runs).  Heuristic admissibility is asserted by exact
// value on constructed inputs (missing_level × book_multiplier), and
// admissible_forge_cost ≤ real forge cost on a conflict-drop case.
// =============================================================================

#define BESQ_TEST_MAIN
#include "framework/test_framework.h"
#include "domain/algorithm/components/Heuristic.h"
#include "domain/algorithm/components/ItemPool.h"
#include "domain/algorithm/components/SearchUtils.h"
#include "domain/algorithm/forge_engine/ForgeEngine.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Equipment.h"
#include "domain/algorithm/types/Item.h"
#include "domain/business/types/Enchantment.h"
#include "domain/business/types/EquipmentTag.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

// Compact ids after sorting by NSID id part:
//   0 = bane_of_arthropods, 1 = knockback, 2 = sharpness
constexpr int16_t ID_BANE       = 0;
constexpr int16_t ID_KNOCKBACK  = 1;
constexpr int16_t ID_SHARPNESS  = 2;

algorithm::Ench E(int16_t id, int16_t lvl) {
    return algorithm::Ench{static_cast<algorithm::Ench::value_type>(id),
                           static_cast<algorithm::Ench::value_type>(lvl)};
}

void build_reg(algorithm::EnchReg& reg) {
    std::vector<EnchInfo> dom;
    dom.emplace_back(NSID("bane_of_arthropods"), "Bane", MCE::All, 5, 5, 1, false,
                     std::unordered_set<NSID>{NSID("sharpness")},
                     std::unordered_set<NSID>{EquipmentTag::sword()});
    dom.emplace_back(NSID("knockback"), "Knockback", MCE::All, 2, 2, 2, false,
                     std::unordered_set<NSID>{},
                     std::unordered_set<NSID>{EquipmentTag::sword()});
    dom.emplace_back(NSID("sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
                     std::unordered_set<NSID>{NSID("bane_of_arthropods")},
                     std::unordered_set<NSID>{EquipmentTag::sword()});

    std::vector<std::pair<std::string, EnchInfo>> sorted;
    for (const auto& i : dom) {
        const auto s = i.id.str();
        const auto p = s.find(':');
        sorted.emplace_back(p != std::string::npos ? s.substr(p + 1) : s, i);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    algorithm::Equipment eq;
    eq.id = "test";
    eq.max_durability = 1561;
    for (int32_t i = 0; i < static_cast<int32_t>(sorted.size()); ++i)
        if (sorted[i].second.supported_items.count(EquipmentTag::sword()) > 0)
            eq.applicable_enchs.insert(static_cast<int16_t>(i));

    std::vector<NSID> gids;
    for (int32_t i = 0; i < static_cast<int32_t>(sorted.size()); ++i)
        gids.push_back(NSID(sorted[i].first));

    std::vector<algorithm::mask_type> exc(sorted.size(), 0);
    for (int32_t i = 0; i < static_cast<int32_t>(sorted.size()); ++i)
        for (int32_t j = i + 1; j < static_cast<int32_t>(sorted.size()); ++j) {
            const bool c = sorted[i].second.exclusive_set.count(NSID(sorted[j].first)) ||
                           sorted[j].second.exclusive_set.count(NSID(sorted[i].first));
            if (c) {
                exc[i] |= (algorithm::mask_type(1) << j);
                exc[j] |= (algorithm::mask_type(1) << i);
            }
        }

    std::vector<algorithm::EnchInfo> compact;
    for (int32_t i = 0; i < static_cast<int32_t>(sorted.size()); ++i) {
        const auto& ei = sorted[i].second;
        algorithm::EnchInfo info;
        info.id         = static_cast<uint8_t>(i);
        info.mul        = static_cast<uint8_t>(ei.multiplier);
        info.mul_b      = static_cast<uint8_t>(std::max(1, ei.multiplier >> 1));
        info.max_lvl    = static_cast<uint8_t>(ei.max_level);
        info.exc_mask   = exc[i];
        info.applicable = ei.supported_items.count(EquipmentTag::sword()) > 0;
        compact.push_back(std::move(info));
    }
    reg.init(std::move(compact), std::move(gids), eq);
}

algorithm::Item equip(std::initializer_list<algorithm::Ench> enchs) {
    algorithm::Item it{algorithm::ItemType::Equip, 1561, 0, {}};
    for (auto& e : enchs) it.enchs.insert(e);
    return it;
}

algorithm::Item book(std::initializer_list<algorithm::Ench> enchs) {
    algorithm::Item it{algorithm::ItemType::Book, 0, 0, {}};
    for (auto& e : enchs) it.enchs.insert(e);
    return it;
}

// ─── HeuristicBasic (item-vector based) ───────────────────────────────

TEST_CASE("test_heuristic_basic") {
    algorithm::EnchReg reg;
    build_reg(reg);
    std::vector<int16_t> buf, dirty;

    std::vector<algorithm::Item> none;
    expect(algorithm::HeuristicBasic::compute(none, reg, {E(ID_SHARPNESS, 5)}, buf, dirty) == 0,
           "HeuristicBasic: empty items → 0");

    std::vector<algorithm::Item> partial{equip({E(ID_SHARPNESS, 2)})};
    expect(algorithm::HeuristicBasic::compute(partial, reg, {E(ID_SHARPNESS, 5)}, buf, dirty) == 3,
           "HeuristicBasic: sharp2 → missing 3 × mul_b(1) = 3");

    std::vector<algorithm::Item> full{equip({E(ID_SHARPNESS, 5)})};
    expect(algorithm::HeuristicBasic::compute(full, reg, {E(ID_SHARPNESS, 5)}, buf, dirty) == 0,
           "HeuristicBasic: full coverage → 0");

    // sharpness 2 (miss 3×1) + knockback 1 (miss 1×1) = 4
    std::vector<algorithm::Item> two{equip({E(ID_SHARPNESS, 2), E(ID_KNOCKBACK, 1)})};
    expect(algorithm::HeuristicBasic::compute(
               two, reg, {E(ID_SHARPNESS, 5), E(ID_KNOCKBACK, 2)}, buf, dirty) == 4,
           "HeuristicBasic: multi-enchant h");
    TEST_PASS("HeuristicBasic::compute");
}

// ─── Heuristic (pool-based) ───────────────────────────────────────────

TEST_CASE("test_heuristic_pool") {
    algorithm::EnchReg reg;
    build_reg(reg);
    algorithm::ItemPool pool;
    const auto id = pool.add(equip({E(ID_SHARPNESS, 2)}));
    std::vector<int16_t> buf, dirty;
    std::vector<algorithm::ItemPool::ItemID> ids{id};

    expect(algorithm::Heuristic::compute(ids, pool, reg, {E(ID_SHARPNESS, 5)}, buf, dirty) == 3,
           "Heuristic: sharp2 → h=3");

    std::vector<algorithm::ItemPool::ItemID> none;
    expect(algorithm::Heuristic::compute(none, pool, reg, {E(ID_SHARPNESS, 5)}, buf, dirty) == 0,
           "Heuristic: empty ids → 0");
    TEST_PASS("Heuristic::compute");
}

// ─── ItemPool ─────────────────────────────────────────────────────────

TEST_CASE("test_item_pool") {
    algorithm::ItemPool pool;
    pool.set_max(2);

    const auto a = pool.add(equip({E(ID_SHARPNESS, 5)}));  // id 0, size 1
    const auto b = pool.add(equip({E(ID_SHARPNESS, 5)}));  // identical → dedup
    expect_eq(a, b, "ItemPool: identical items dedup to the same id");
    expect(pool.size() == 1, "ItemPool: size 1 after dedup");

    const auto c = pool.add(equip({E(ID_KNOCKBACK, 2)}));  // id 1, size 2
    expect(c != algorithm::ItemPool::INVALID_ITEM_ID, "ItemPool: second distinct item fits");
    const auto d = pool.add(equip({E(ID_KNOCKBACK, 1)}));  // full (size 2 ≥ max 2)
    expect_eq(d, algorithm::ItemPool::INVALID_ITEM_ID, "ItemPool: full → INVALID_ITEM_ID");
    expect(pool.size() == 2, "ItemPool: size 2 at capacity");

    std::vector<algorithm::ItemPool::ItemID> v1{a};
    expect_eq(pool.hash_ids(v1), pool.hash_ids(v1), "ItemPool: hash_ids deterministic");
    const auto v2 = pool.add(equip({E(ID_KNOCKBACK, 2)}));  // dedup → id 1
    expect_eq(c, v2, "ItemPool: re-adding identical content returns the same id");

    pool.clear();
    expect(pool.size() == 0, "ItemPool: clear resets size");
    const auto e = pool.add(equip({E(ID_SHARPNESS, 1)}));
    expect(e != algorithm::ItemPool::INVALID_ITEM_ID, "ItemPool: reusable after clear");
    TEST_PASS("ItemPool");
}

// ─── SearchUtils ──────────────────────────────────────────────────────

TEST_CASE("test_meets_target") {
    algorithm::EnchReg reg;
    build_reg(reg);
    const algorithm::Item t = equip({E(ID_SHARPNESS, 5), E(ID_KNOCKBACK, 2)});
    expect(algorithm::meets_target(equip({E(ID_SHARPNESS, 5), E(ID_KNOCKBACK, 2)}), t),
           "meets_target: exact");
    expect(algorithm::meets_target(equip({E(ID_SHARPNESS, 6), E(ID_KNOCKBACK, 2)}), t),
           "meets_target: over-level");
    expect(!algorithm::meets_target(equip({E(ID_SHARPNESS, 4), E(ID_KNOCKBACK, 2)}), t),
           "meets_target: under-level");
    expect(!algorithm::meets_target(equip({E(ID_SHARPNESS, 5)}), t),
           "meets_target: missing enchant");
    expect(!algorithm::meets_target(book({E(ID_SHARPNESS, 5), E(ID_KNOCKBACK, 2)}), t),
           "meets_target: wrong item type");
    TEST_PASS("meets_target");
}

TEST_CASE("test_merge_wastes_target") {
    algorithm::EnchReg reg;
    build_reg(reg);
    const algorithm::Item target = equip({E(ID_SHARPNESS, 5), E(ID_BANE, 2)});

    // base has sharpness, sac carries bane (conflicts with sharpness) → wasted.
    const algorithm::Item base = equip({E(ID_SHARPNESS, 3)});
    const algorithm::Item sac  = book({E(ID_BANE, 2)});
    expect(algorithm::merge_wastes_target(base, sac, target, reg),
           "merge_wastes_target: sac has a target enchant conflicting with base");

    // base has knockback (no conflict with bane) → not wasted.
    const algorithm::Item base2 = equip({E(ID_KNOCKBACK, 1)});
    expect(!algorithm::merge_wastes_target(base2, sac, target, reg),
           "merge_wastes_target: no conflict → not wasted");
    TEST_PASS("merge_wastes_target");
}

TEST_CASE("test_admissible_forge_cost") {
    algorithm::EnchReg reg;
    build_reg(reg);
    algorithm::ForgeConfig cfg;
    cfg.platform = MCE::Java;
    algorithm::ForgeEngine engine{cfg};

    // target has sharpness; sac book has bane (conflict).  estimate over-charges
    // bane (4×mul_b); admissible subtracts it → 0 ≤ real forge cost (popcount=1).
    const algorithm::Item target = equip({E(ID_SHARPNESS, 5)});
    const algorithm::Item sac    = book({E(ID_BANE, 4)});
    const int32_t est  = engine.estimate_forge_cost(target, sac, reg);
    const int32_t adm  = algorithm::admissible_forge_cost(engine, target, sac, reg);
    const int32_t real = engine.forge(target, sac, reg).second;
    expect(adm == 0, "admissible_forge_cost: conflict subtracts the full bane cost");
    expect(adm <= real, "admissible_forge_cost ≤ real forge cost (admissible)");
    expect(est > real, "plain estimate over-charges on conflict (sanity for the test)");

    // Clean Java same-enchant merge: admissible == estimate == real.
    const algorithm::Item sac2 = book({E(ID_SHARPNESS, 3)});
    const int32_t adm2 = algorithm::admissible_forge_cost(engine, equip({}), sac2, reg);
    expect(adm2 == 3, "admissible_forge_cost: clean sharp3 merge = 3×mul_b");
    TEST_PASS("admissible_forge_cost");
}

TEST_CASE("test_dfs_bound") {
    algorithm::EnchReg reg;
    build_reg(reg);
    algorithm::ForgeConfig cfg;
    cfg.platform = MCE::Java;
    algorithm::ForgeEngine engine{cfg};
    std::vector<int16_t> hbuf, hdirty;

    // 1-step bound: empty base + sharpness 5 book → cost 5.
    int64_t limit = 1000;
    std::vector<algorithm::Item> items{equip({}), book({E(ID_SHARPNESS, 5)})};
    const int32_t bound = algorithm::search_utils::dfs_bound(
        items, 0, INT32_MAX, limit, engine, reg, equip({E(ID_SHARPNESS, 5)}), hbuf, hdirty);
    expect_eq(bound, 5, "dfs_bound finds the 1-step bound (cost 5)");

    // node_limit exhausted → returns best_cost unchanged.
    int64_t zero = 0;
    const int32_t before = 12345;
    const int32_t out = algorithm::search_utils::dfs_bound(
        items, 0, before, zero, engine, reg, equip({E(ID_SHARPNESS, 5)}), hbuf, hdirty);
    expect_eq(out, before, "dfs_bound: node_limit ≤ 0 returns best_cost unchanged");
    TEST_PASS("dfs_bound");
}

}  // anonymous namespace
