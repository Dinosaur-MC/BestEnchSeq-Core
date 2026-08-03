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

#include "framework/test_utils.h"
#include "domain/orchestration/pipelines/SolvePipeline.h"
#include "domain/algorithm/plugin/AlgorithmLoader.h"
#include "domain/interface/cli/ItemParser.h"
#include "domain/business/types/Profile.h"
#include "builtin/DataLoader.h"
#include "builtin/I18nLoader.h"
#include "common/i18n/Language.h"
#include <string>

namespace {

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

SolveRequest direct_request(const Profile& profile, const std::string& target,
                            const std::string& algo) {
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
        SolvePipeline::run(profile, req, loader);
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
        SolvePipeline::run(profile, req, loader);
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
        auto result = SolvePipeline::run(profile, req, loader);
        empty_result = !result.success && result.solutions.empty();
    } catch (...) {
        rejected = true;
    }
    expect(empty_result || rejected, "conflicting target is not solvable");
    TEST_PASS("solve pipeline: conflicting target not solvable");
}

}  // anonymous namespace

int main() {
    register_builtin_translations(LanguageManager::instance());
    LanguageManager::instance().select("en_US");

    try {
        test_unknown_algo();
        test_unsupported_mode();
        test_conflicting_target_not_solvable();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
