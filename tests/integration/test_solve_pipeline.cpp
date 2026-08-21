// =============================================================================
// SolvePipeline error-path tests.
//
// The orchestration SolvePipeline's failure branches were only reachable
// indirectly (via BesqContext::solve / the CLI).  These drive them directly:
//   • unknown algorithm   → pipeline.err.unknown_algo throw
//   • unsupported mode    → pipeline.err.unsupported_mode throw (dp_merge is
//                           direct-only, hamming is direct|inventory)
//   • unreachable target  → simulate()==false → empty result (success=false)
// =============================================================================

#define BESQ_TEST_MAIN

#include "domain/algorithm/_strategies/dp_merge/DPMergeAlgorithm.h"
#include "domain/algorithm/_strategies/dp_merge/DPMergeStateSerializer.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Equipment.h"
#include "domain/business/loaders/BuiltinData.h"
#include "domain/interface/BesqContext.h"
#include "domain/interface/components/BuiltinI18n.h"
#include "common/i18n/Language.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "domain/business/types/Profile.h"
#include "domain/interface/cli/ItemParser.h"
#include "domain/orchestration/pipelines/SolvePipeline.h"
#include "domain/orchestration/types/SolveSnapshot.h"
#include "framework/test_framework.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

/// Run the production pipeline path: build the pruned snapshot first (which is
/// also where unknown/over-max enchants are validated), then run on it.
SolveResult run_solve(const Profile& profile, const SolveRequest& req, algorithm::AlgorithmLoader& loader) {
    auto snap = orchestration::build_solve_snapshot(req, profile);
    return SolvePipeline::run(snap, req, loader);
}

Profile make_builtin_profile() {
    TagRegistry cat_reg;
    EnchantmentRegistry ench_reg;
    EquipmentRegistry eq_reg;
    besq::data::load_builtin_data(cat_reg, ench_reg, eq_reg);

    ProfileMetadata meta("test:sp");
    Profile profile(std::move(meta), std::move(ench_reg), std::move(eq_reg), std::move(cat_reg));
    profile.set_tag_resolver(besq::data::make_builtin_tag_resolver());
    return profile;
}

SolveRequest direct_request(const Profile& profile, const std::string& target, const std::string& algo) {
    SolveRequest req;
    req.target_item = ItemParser::parse(target, profile.ench(), profile.eq());
    req.mode = AlgorithmMode::direct;
    req.payload = DirectPayload{};
    req.algorithm = algo;
    req.forge_config.platform = MCE::Java;
    return req;
}

void test_unknown_algo() {
    auto profile = make_builtin_profile();
    algorithm::AlgorithmLoader loader;
    loader.load_builtin();
    auto req = direct_request(profile, "diamond_sword[sharpness=3]", "nope");

    bool threw = false;
    try {
        run_solve(profile, req, loader);
    } catch (const std::runtime_error& e) {
        threw = std::string(e.what()).find("Unknown algorithm") != std::string::npos;
    }
    expect(threw, "unknown algorithm name throws pipeline.err.unknown_algo");
    TEST_PASS("solve pipeline: unknown algorithm");
}

void test_unsupported_mode() {
    auto profile = make_builtin_profile();
    algorithm::AlgorithmLoader loader;
    loader.load_builtin();
    // dp_merge is direct-only → inventory request must be rejected.
    auto req = direct_request(profile, "diamond_sword[sharpness=3]", "dp_merge");
    req.mode = AlgorithmMode::inventory;
    req.payload = InventoryPayload{};

    bool threw = false;
    try {
        run_solve(profile, req, loader);
    } catch (const std::runtime_error& e) {
        threw = std::string(e.what()).find("does not support") != std::string::npos;
    }
    expect(threw, "dp_merge + inventory mode throws pipeline.err.unsupported_mode");
    TEST_PASS("solve pipeline: unsupported mode");
}

void test_conflicting_target_not_solvable() {
    auto profile = make_builtin_profile();
    algorithm::AlgorithmLoader loader;
    loader.load_builtin();
    // sharpness and smite are mutually exclusive → simulate()==false.  The
    // pipeline must not produce a solution (empty result or an apply-time
    // rejection — either way it is not a solvable plan).
    auto req = direct_request(profile, "diamond_sword[sharpness=5,smite=5]", "dp_merge");

    bool empty_result = false;
    bool rejected = false;
    try {
        auto result = run_solve(profile, req, loader);
        empty_result = !result.success && result.solutions.empty();
    } catch (...) {
        rejected = true;
    }
    expect(empty_result || rejected, "conflicting target is not solvable");
    TEST_PASS("solve pipeline: conflicting target not solvable");
}

void test_resume_from_checkpoint() {
    // Build a dp_merge checkpoint (small direct-mode input, pre-run so the
    // memo cache is populated), write it to a temp file, then resume it via
    // BesqContext::solve_from_checkpoint — the full CLI --resume / HTTP
    // service restore path: peek → create executor from tag → start(blob) → recall.
    // NOTE: compact types are explicitly qualified (business/orchestration
    // expose same-named global types, so `using namespace algorithm` is
    // ambiguous here).
    algorithm::DPMergeStateSerializer ser;
    algorithm::DPMergeAlgorithm algo;

    std::vector<algorithm::EnchInfo> infos(1);
    infos[0].id = 0;
    infos[0].mul = 1;
    infos[0].mul_b = 1;
    infos[0].max_lvl = 5;
    infos[0].exc_mask = 0;
    infos[0].applicable = true;
    algorithm::Equipment eq;
    eq.id = NSID("minecraft:diamond_sword");
    eq.max_durability = 1561;
    eq.applicable_enchs.insert(0);

    algorithm::AlgorithmInput input;
    input.config.forge.platform = MCE::Java;
    input.config.mode = AlgorithmMode::direct; // global enum (CommonTypes.h)
    input.registry.init(std::move(infos), {NSID("minecraft:sharpness")}, eq);
    input.data = algorithm::DirectPayload{};
    input.target.type = algorithm::ItemType::Equip;
    input.target.enchs.insert(algorithm::Ench{0, 5});
    algorithm::ExecutionContext ctx(0, "dp_merge");
    algo.init(input, ctx);
    algo.execute(input, ctx);
    auto blob = ser.serialize(algo, input);
    expect(!blob.empty(), "checkpoint serialized");

    const auto tmp = std::filesystem::temp_directory_path() / "besq_ckpt_resume.ckpt";
    {
        std::ofstream out(tmp, std::ios::binary);
        out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
        expect(static_cast<bool>(out), "checkpoint file written");
    }

    BesqContext bctx;
    bctx.load_builtin();
    auto ck = bctx.solve_from_checkpoint(tmp.string());
    expect(ck.result.success && !ck.result.solutions.empty(),
           "resumed solve produces solutions");
    expect(ck.result.algorithm_used == "dp_merge", "algorithm taken from checkpoint tag");
    expect(ck.mode == AlgorithmMode::direct, "mode taken from checkpoint input");

    // Invalid file → clear error (not a crash / empty result).
    bool threw = false;
    try {
        (void)bctx.solve_from_checkpoint("no_such_ckpt_file_xyz.ckpt");
    } catch (const std::exception&) {
        threw = true;
    }
    expect(threw, "missing checkpoint file throws");

    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    TEST_PASS("solve_from_checkpoint: resume produces a full result");
}

/// T3 持久化门（spec §3.5 / plan Task 3）：`save_solve_state`（^S 落盘路径）
/// → `solve_from_checkpoint`（--resume 恢复路径）API 级往返。^S 与 --resume
/// 依赖的正是这条持久化契约：暂停中的求解序列化到磁盘，新上下文（模拟新
/// 进程）从同一冻结点恢复并跑出与原求解一致的结果。
///
/// 流程：1) 后台线程跑 dp_merge（自定义魔咒目标，搜索足够久以便暂停可靠）；
/// 2) 主线程 pause_solve() 至 Paused → save_solve_state(ckpt) 断言成功且文件
/// 非空；3) resume 原求解至完成（result_orig）；4) 全新 BesqContext
/// solve_from_checkpoint(ckpt)（result_resumed）；5) 断言两结果关键属性一致
/// （success / solutions 数 / 最优解成本 / algorithm / mode）——同一冻结状态
/// 确定性续算必须产出相同结果。
void test_save_solve_state_roundtrip() {
    BesqContext ctx;
    ctx.load_builtin();

    // 自定义剑魔咒（镜像 test_besq_abort_concurrent 的 in-flight 模式）：足够
    // 多的目标魔咒让 dp_merge 指数搜索在暂停窗口内保持 Running，又不至于让
    // 完整求解超出测试预算（web 测试的 18 魔咒重任务在 Debug 下远超预算）。
    constexpr int kEnchCount = 12;
    for (int i = 0; i < kEnchCount; ++i) {
        EnchInfo info;
        info.id = NSID("test:ench_" + std::to_string(i));
        info.name = "Test Ench " + std::to_string(i);
        info.max_level = 5;
        info.multiplier = 1;
        info.supported_items.insert(NSID("#minecraft:swords"));
        expect(ctx.add_enchantment(info), "add custom enchantment test:ench_" + std::to_string(i));
    }

    Item target_item;
    target_item.id = NSID("minecraft:diamond_sword");
    for (int i = 0; i < kEnchCount; ++i)
        target_item.enchantments.emplace(NSID("test:ench_" + std::to_string(i)), 5);
    if (auto eq_it = ctx.equipment().find(NSID("minecraft:diamond_sword")); eq_it != ctx.equipment().end())
        target_item.durability = eq_it->max_durability;

    SolveRequest request;
    request.target_item = target_item;
    request.mode = AlgorithmMode::direct;
    request.payload = DirectPayload{};
    request.algorithm = "dp_merge";
    request.forge_config.platform = MCE::Java;
    request.search_config.max_solutions = 4;
    // 安全上限：若 pause/save 失败，求解不能无限跑（框架 per-case 超时兜底）。
    request.search_config.max_search_time = std::chrono::milliseconds(30000);

    const std::string ck_path = (std::filesystem::temp_directory_path() / "besq_ckpt_roundtrip.ckpt").string();
    std::error_code ec;
    std::filesystem::remove(ck_path, ec);

    // ── 原求解：后台线程 + 暂停 + 保存 + 恢复至完成 ──
    // 每轮尝试启动一个新鲜求解；pause_solve() 阻塞至 quiesced（Completed 上
    // 是 no-op）→ 检查 Paused；已暂停则 save（成功即退出循环，线程保持存活
    // 至 resume 后 join）；未暂停/保存失败则 abort + join 后重试（与 web 测试
    // 的 pause 重试模式一致，防快机器上求解抢先完成）。
    SolveResult orig;
    std::string solve_error;
    bool saved = false;
    std::thread solver;
    for (int attempt = 0; attempt < 3 && !saved; ++attempt) {
        std::atomic<bool> done{false};
        solver = std::thread([&] {
            try {
                orig = ctx.solve(request);
            } catch (const std::exception& e) {
                solve_error = e.what();
            }
            done = true;
        });

        // 等 executor handle 发布（state != Idle，或求解已结束）
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline && !done.load() &&
               ctx.solve_progress().state == algorithm::AlgorithmState::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ctx.pause_solve();
        const bool paused = ctx.solve_progress().state == algorithm::AlgorithmState::Paused;
        if (paused)
            saved = ctx.save_solve_state(ck_path);
        if (saved) {
            ctx.resume_solve();   // 原求解从同一冻结点继续 → 完成
            solver.join();
            break;
        }
        ctx.abort_solve();        // 未暂停或保存失败 → 取消本尝试
        solver.join();
    }
    expect(saved, "solve paused + save_solve_state roundtrip (retry loop)");
    expect(solve_error.empty(), "original solve must not throw (got: " + solve_error + ")");
    expect(orig.success && !orig.solutions.empty(), "original solve completed with solutions");
    expect(std::filesystem::exists(ck_path), "checkpoint file written");
    expect(std::filesystem::file_size(ck_path) > 0, "checkpoint file non-empty");

    // ── 新上下文（模拟新进程）：从磁盘 checkpoint 恢复 ──
    BesqContext ctx2;
    ctx2.load_builtin();
    auto ck = ctx2.solve_from_checkpoint(ck_path);
    expect(ck.result.success && !ck.result.solutions.empty(), "resumed solve produces solutions");
    expect(ck.result.algorithm_used == "dp_merge", "algorithm taken from checkpoint tag");
    expect(ck.mode == AlgorithmMode::direct, "mode taken from checkpoint input");

    // ── 恢复结果与原求解关键属性一致（同一冻结状态确定性续算）──
    expect(ck.result.solutions.size() == orig.solutions.size(),
           "resumed solution count matches original");
    if (!ck.result.solutions.empty() && !orig.solutions.empty())
        expect(ck.result.solutions[0].total_exp_level_cost == orig.solutions[0].total_exp_level_cost,
               "resumed best-solution cost matches original");

    std::filesystem::remove(ck_path, ec);
    TEST_PASS("save_solve_state → solve_from_checkpoint roundtrip");
}

} // anonymous namespace

TEST_CASE_TIMEOUT("test_solve_pipeline", 180) {
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");

    test_unknown_algo();
    test_unsupported_mode();
    test_conflicting_target_not_solvable();
    test_resume_from_checkpoint();
    test_save_solve_state_roundtrip();
}
