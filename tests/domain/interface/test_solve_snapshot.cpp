// =============================================================================
// SolveSnapshot：按请求剪枝的有效视图快照（P0 锁攻破）
// =============================================================================
#define BESQ_TEST_MAIN
#include "domain/interface/BesqContext.h"
#include "domain/orchestration/types/SolveSnapshot.h"
#include "framework/test_framework.h"
#include <string>

TEST_CASE("test_solve_snapshot") {
    BesqContext ctx;
    ctx.load_builtin();
    SolveRequest req;
    req.mode = AlgorithmMode::direct;
    req.target_item = Item{NSID("minecraft:diamond_sword"), EnchSet{{NSID("minecraft:sharpness"), 5}}, 0, 1561};
    req.payload = DirectPayload{EnchSet{{NSID("minecraft:sharpness"), 2}}};
    req.algorithm = "dp_merge";

    auto snap = ctx.solve_snapshot(req);
    expect(snap.ench().contains(NSID("minecraft:sharpness")), "sharpness in snapshot");
    expect(snap.ench().contains(NSID("minecraft:smite")), "exclusive-set member pulled in");
    expect(snap.eq().contains(NSID("minecraft:diamond_sword")), "target equipment in snapshot");
    expect(!snap.ench().contains(NSID("minecraft:unbreaking")), "unreferenced enchant pruned");
    expect(!snap.eq().contains(NSID("minecraft:iron_sword")), "unreferenced equipment pruned");
    // sharpness 的 supported_items 是 `#minecraft:enchantable/sharp_weapon`——
    // 快照解析器须可查该 tag，且 BFS 嵌套展开后成员含 diamond_sword
    const auto* sharp_weapon = snap.tag_resolver().get_tag("minecraft", "enchantable/sharp_weapon");
    expect(sharp_weapon != nullptr, "supported-items tag resolvable in snapshot");
    if (sharp_weapon)
        expect(sharp_weapon->count("minecraft:diamond_sword") == 1, "tag resolves to diamond_sword");
    SolveRequest bad = req;
    bad.target_item = Item{NSID("minecraft:diamond_sword"), EnchSet{{NSID("minecraft:no_such_ench"), 1}}, 0, 1561};
    bool threw = false;
    try {
        (void)ctx.solve_snapshot(bad);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "unknown enchant throws during snapshot build");
    TEST_PASS("test_solve_snapshot");
}

TEST_CASE("test_solve_snapshot_inventory") {
    BesqContext ctx;
    ctx.load_builtin();
    SolveRequest req;
    req.mode = AlgorithmMode::inventory;
    req.target_item = Item{NSID("minecraft:diamond_sword"), EnchSet{{NSID("minecraft:sharpness"), 5}}, 0, 1561};
    InventoryPayload payload;
    payload.extra_items.emplace_back(NSID("minecraft:iron_sword"), EnchSet{{NSID("minecraft:smite"), 2}}, 1, 251);
    payload.extra_item_priorities.push_back(1);
    req.payload = payload;
    req.algorithm = "hamming";

    auto snap = ctx.solve_snapshot(req);
    expect(snap.eq().contains(NSID("minecraft:iron_sword")), "inventory item equipment in snapshot");
    expect(snap.ench().contains(NSID("minecraft:smite")), "inventory item enchant in snapshot");

    SolveRequest bad = req;
    InventoryPayload bad_payload;
    bad_payload.extra_items.emplace_back(NSID("minecraft:no_such_item"), EnchSet{}, 0, 10);
    bad_payload.extra_item_priorities.push_back(0);
    bad.payload = bad_payload;
    bool threw = false;
    try {
        (void)ctx.solve_snapshot(bad);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "unknown inventory equipment throws during snapshot build");
    TEST_PASS("test_solve_snapshot_inventory");
}

TEST_CASE("test_solve_snapshot_max_level") {
    BesqContext ctx;
    ctx.load_builtin();
    SolveRequest req;
    req.mode = AlgorithmMode::direct;
    req.target_item = Item{NSID("minecraft:diamond_sword"), EnchSet{{NSID("minecraft:sharpness"), 5}}, 0, 1561};
    req.payload = DirectPayload{EnchSet{{NSID("minecraft:sharpness"), 2}}};
    req.algorithm = "dp_merge";

    SolveRequest over_target = req;
    over_target.target_item = Item{NSID("minecraft:diamond_sword"), EnchSet{{NSID("minecraft:sharpness"), 6}}, 0, 1561};
    bool threw = false;
    try {
        (void)ctx.solve_snapshot(over_target);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "target level above max_level throws during snapshot build");

    SolveRequest over_source = req;
    over_source.payload = DirectPayload{EnchSet{{NSID("minecraft:sharpness"), 9}}};
    threw = false;
    try {
        (void)ctx.solve_snapshot(over_source);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "source level above max_level throws during snapshot build");
    TEST_PASS("test_solve_snapshot_max_level");
}
