// =============================================================================
// DPMergeAlgorithm correctness tests.
//
// dp_merge is a divide-&-conquer DP with Pareto-bucketed frontiers and both a
// flat bitmask cache (n≤20) and a map-backed cache.  Those internals are
// private, so correctness is asserted through the public IAlgorithm::execute
// seam:
//   * optimal-cost equivalence against bb_dp (an independently-implemented
//     exact solver) across a deterministic corpus — this also guards the
//     Pareto pruning (a bad prune would change the optimum);
//   * determinism across runs;
//   * best-cost invariance to max_solutions.
// =============================================================================

#include "framework/test_utils.h"
#include "domain/algorithm/AlgorithmExecutor.h"
#include "domain/algorithm/registries/EnchReg.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Equipment.h"
#include "domain/algorithm/types/Item.h"
#include "domain/algorithm/_strategies/dp_merge/DPMergeAlgorithm.h"
#include "domain/algorithm/_strategies/bb_dp/BBDpAlgorithm.h"
#include "domain/business/types/Enchantment.h"
#include "domain/business/types/EquipmentTag.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

// Compact ids after sorting the registry by NSID id part:
//   0 = bane_of_arthropods, 1 = knockback, 2 = sharpness, 3 = unbreaking
constexpr int16_t ID_BANE       = 0;
constexpr int16_t ID_KNOCKBACK  = 1;
constexpr int16_t ID_SHARPNESS  = 2;
constexpr int16_t ID_UNBREAKING = 3;

// Ench with explicit value_type casts (avoid narrowing in braced init).
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
    dom.emplace_back(NSID("unbreaking"), "Unbreaking", MCE::All, 3, 3, 1, false,
                     std::unordered_set<NSID>{},
                     std::unordered_set<NSID>{EquipmentTag::sword()});

    // Sort by NSID id part for stable compact ids.
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

    // Pairwise exclusive-set → conflict mask.
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

algorithm::Item make_target(const std::vector<algorithm::Ench>& wanted) {
    algorithm::Item t{algorithm::ItemType::Equip, 1561, 0, {}};
    for (const auto& e : wanted) t.enchs.insert(e);
    return t;
}

int32_t run_algo(std::unique_ptr<algorithm::IAlgorithm> algo,
                 const algorithm::EnchReg& reg, const algorithm::Item& target,
                 const algorithm::EnchCollection& source = {},
                 int32_t max_solutions = 0) {
    algorithm::AlgorithmExecutor executor(std::move(algo));
    algorithm::AlgorithmInput input;
    input.config.forge.platform = MCE::Java;
    input.config.mode = AlgorithmMode::direct;
    if (max_solutions > 0) input.config.search.max_solutions = max_solutions;
    input.registry = reg;
    input.target   = target;
    input.data     = algorithm::DirectPayload{source};

    executor.start(std::move(input));
    auto state = executor.wait();
    if (state != algorithm::AlgorithmState::Completed) return -1;
    auto out = executor.output();
    if (out.solutions.empty()) return -1;
    if (out.solutions[0].steps.empty()) return 0;
    return out.solutions[0].total_cost;
}

void check_equivalence(const algorithm::EnchReg& reg,
                       const algorithm::Item& target,
                       const algorithm::EnchCollection& source,
                       const std::string& label) {
    const auto dp = run_algo(std::make_unique<algorithm::DPMergeAlgorithm>(), reg, target, source);
    const auto bb = run_algo(std::make_unique<algorithm::BBDpAlgorithm>(), reg, target, source);
    expect_eq(dp, bb, "dp_merge == bb_dp optimal cost: " + label);
    std::cout << "  [" << label << "] cost="
              << (dp < 0 ? std::string("unreachable") : std::to_string(dp))
              << std::endl;
}

void test_equivalence_corpus(const algorithm::EnchReg& reg) {
    check_equivalence(reg, make_target({E(ID_SHARPNESS, 5)}), {}, "sharp5");
    check_equivalence(reg, make_target({E(ID_KNOCKBACK, 2)}), {}, "knock2");
    check_equivalence(reg, make_target({E(ID_UNBREAKING, 3)}), {}, "unbreak3");
    check_equivalence(reg, make_target({E(ID_BANE, 5)}), {}, "bane5");
    check_equivalence(reg, make_target({E(ID_SHARPNESS, 5), E(ID_KNOCKBACK, 2)}), {},
                      "sharp5+knock2");
    check_equivalence(reg,
                      make_target({E(ID_SHARPNESS, 3), E(ID_UNBREAKING, 3), E(ID_KNOCKBACK, 2)}),
                      {}, "3-enchant");
    check_equivalence(reg, make_target({E(ID_SHARPNESS, 5), E(ID_BANE, 5)}), {},
                      "conflict-unreachable");
    check_equivalence(reg, make_target({E(ID_SHARPNESS, 3)}), {E(ID_SHARPNESS, 2)},
                      "pre-enchanted base");
    check_equivalence(reg, make_target({E(ID_SHARPNESS, 3), E(ID_KNOCKBACK, 2)}),
                      {E(ID_SHARPNESS, 2), E(ID_KNOCKBACK, 1)}, "pre-enchanted base 2");
    TEST_PASS("dp_merge == bb_dp optimal cost (deterministic corpus)");
}

void test_determinism(const algorithm::EnchReg& reg) {
    const auto t = make_target({E(ID_SHARPNESS, 5), E(ID_KNOCKBACK, 2)});
    const auto a = run_algo(std::make_unique<algorithm::DPMergeAlgorithm>(), reg, t);
    const auto b = run_algo(std::make_unique<algorithm::DPMergeAlgorithm>(), reg, t);
    expect_eq(a, b, "dp_merge deterministic across runs");
    TEST_PASS("dp_merge determinism");
}

void test_max_solutions_invariance(const algorithm::EnchReg& reg) {
    const auto t = make_target({E(ID_SHARPNESS, 5), E(ID_KNOCKBACK, 2), E(ID_UNBREAKING, 3)});
    const auto s1 = run_algo(std::make_unique<algorithm::DPMergeAlgorithm>(), reg, t, {}, 1);
    const auto s8 = run_algo(std::make_unique<algorithm::DPMergeAlgorithm>(), reg, t, {}, 8);
    expect_eq(s1, s8, "best-cost invariant to max_solutions");
    expect(s1 > 0, "3-enchant target produces a positive cost");
    TEST_PASS("dp_merge best-cost invariance to max_solutions");
}

}  // anonymous namespace

int main() {
    try {
        algorithm::EnchReg reg;
        build_reg(reg);
        test_equivalence_corpus(reg);
        test_determinism(reg);
        test_max_solutions_invariance(reg);
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
