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
