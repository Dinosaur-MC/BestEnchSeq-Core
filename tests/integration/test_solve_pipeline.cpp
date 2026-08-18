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
#include <filesystem>
#include <fstream>
#include <string>

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

} // anonymous namespace

TEST_CASE("test_solve_pipeline") {
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");

    test_unknown_algo();
    test_unsupported_mode();
    test_conflicting_target_not_solvable();
    test_resume_from_checkpoint();
}
