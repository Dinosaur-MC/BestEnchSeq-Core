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

// SolveHistory（计划 B Task B2）：BesqContext 绑定有界求解历史——单参 solve
// 包装（CLI/ABI 路径）记录 Submitted + Completed/Failed；有界环形覆盖 + seq 单调。
TEST_CASE("test_solve_history") {
    BesqContext ctx;
    ctx.load_builtin();

    // ── 1. 一次真实求解（单参包装，内部走快照路径）→ Submitted + Completed ──
    SolveRequest req;
    req.mode = AlgorithmMode::direct;
    req.target_item = Item{NSID("minecraft:diamond_sword"), EnchSet{{NSID("minecraft:sharpness"), 5}}, 0, 1561};
    req.payload = DirectPayload{EnchSet{{NSID("minecraft:sharpness"), 2}}};
    req.algorithm = "dp_merge";
    auto result = ctx.solve(req);
    expect(result.success, "history-cover solve succeeds");

    auto hist = ctx.solve_history();
    expect(hist.size() >= 2, "submitted + completed recorded");
    bool saw_submit = false, saw_done = false;
    for (size_t i = 0; i < hist.size(); ++i) {
        // 快照最新在前：seq 随索引严格递减（事件按发生序单调递增）。
        if (i > 0)
            expect(hist[i].seq < hist[i - 1].seq, "seq strictly monotonic (newest first)");
        if (hist[i].type == SolveEventType::Submitted)
            saw_submit = true;
        if (hist[i].type == SolveEventType::Completed)
            saw_done = true;
    }
    expect(saw_submit && saw_done, "both lifecycle events present");
    expect(hist[0].type == SolveEventType::Completed, "latest event is the terminal one");
    expect(hist[0].task_id.empty(), "CLI events carry empty task_id");
    expect(hist[0].target.find("diamond_sword") != std::string::npos, "completed target summary carries item");
    expect(hist[0].target.find("sharpness") != std::string::npos, "completed target summary carries enchant");
    expect(hist[0].algorithm == "dp_merge" && hist[0].mode == "direct", "algorithm/mode recorded");
    expect(hist[0].timestamp_ms > 0, "timestamp filled");
    expect(hist[0].total_level_cost > 0, "completed carries total_level_cost");
    expect(hist[0].total_exp_cost > 0, "completed carries total_exp_cost");
    expect(hist[0].solution_count > 0, "completed carries solution_count");
    expect(hist[0].computation_ms >= 0, "completed carries computation_ms");

    // ── 2. Failed：非法请求（未知魔咒）→ Submitted + Failed，异常原样重抛 ──
    SolveRequest bad = req;
    bad.target_item = Item{NSID("minecraft:diamond_sword"), EnchSet{{NSID("minecraft:no_such_ench"), 1}}, 0, 1561};
    bool threw = false;
    try {
        (void)ctx.solve(bad);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "invalid solve throws");
    hist = ctx.solve_history();
    expect(hist[0].type == SolveEventType::Failed, "latest event is Failed");
    expect(!hist[0].error_message.empty(), "failed event carries error_message");
    expect(hist[0].seq == hist[1].seq + 1, "failed event seq consecutive with its submitted");

    // ── 3. 有界覆盖：灌入超过容量的合成事件 → 最多 kMaxSolveHistory 条、seq 连续 ──
    for (int i = 0; i < 120; ++i) {
        SolveHistoryEvent ev;
        ev.type = SolveEventType::Submitted;
        ev.target = "synthetic-" + std::to_string(i);
        ctx.record_solve_event(ev);
    }
    hist = ctx.solve_history();
    expect(hist.size() == kMaxSolveHistory, "history bounded at capacity");
    bool seq_consecutive = true;
    for (size_t i = 0; i + 1 < hist.size(); ++i)
        if (hist[i].seq != hist[i + 1].seq + 1)
            seq_consecutive = false;
    expect(seq_consecutive, "remaining window has consecutive seq");
    expect(hist.back().target != "synthetic-0", "oldest synthetic event overwritten");
    TEST_PASS("test_solve_history");
}

// 快照路径求解端到端：solve(request, snapshot) 与旧路径结果一致
TEST_CASE("test_solve_snapshot_solve") {
    BesqContext ctx;
    ctx.load_builtin();
    SolveRequest req;
    req.mode = AlgorithmMode::direct;
    req.target_item = Item{NSID("minecraft:diamond_sword"), EnchSet{{NSID("minecraft:sharpness"), 5}}, 0, 1561};
    req.payload = DirectPayload{EnchSet{{NSID("minecraft:sharpness"), 2}}};
    req.algorithm = "dp_merge";
    auto snap = ctx.solve_snapshot(req);
    auto result = ctx.solve(req, snap);
    expect(result.success, "snapshot-based solve succeeds");
    expect(!result.solutions.empty(), "snapshot-based solve produces solutions");
    expect(result.solutions[0].target_item.id == NSID("minecraft:diamond_sword"), "target preserved");
    TEST_PASS("test_solve_snapshot_solve");
}
