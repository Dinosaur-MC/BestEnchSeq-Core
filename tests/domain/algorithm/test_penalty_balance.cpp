#include "framework/test_utils.h"
#include "penalty_balance/DynamicPenaltyBalancingAlgorithm.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Equipment.h"
#include <memory>
#include <vector>
using namespace algorithm;

// ─── Regression: penalty_balance misread max_solutions as a merge-STEP cap ──
//
// `config.search.max_solutions` is a SOLUTION-count cap (dfs/astar semantics:
// stop after N solutions found).  This greedy emits exactly one solution, so it
// must ignore it.  Before the fix (issue #17) the loop broke after
// `max_solutions` merge STEPS, so with the CLI default --solutions 1 any target
// needing ≥2 merges reported CompleteNoSolution and the target was "unreachable".

void test_penalty_balance_multi_step_with_max_solutions_1() {
    std::vector<EnchInfo> infos(2);
    infos[0].id         = 0;
    infos[0].mul        = 1;
    infos[0].mul_b      = 1;
    infos[0].max_lvl    = 5;
    infos[0].exc_mask   = 0;
    infos[0].applicable = true;
    infos[1].id         = 1;
    infos[1].mul        = 2;
    infos[1].mul_b      = 1;
    infos[1].max_lvl    = 2;
    infos[1].exc_mask   = 0;
    infos[1].applicable = true;

    Equipment eq;
    eq.id             = NSID("test");
    eq.max_durability = 1561;
    eq.applicable_enchs.insert(0);
    eq.applicable_enchs.insert(1);

    AlgorithmInput input;
    input.config.forge.platform      = MCE::Java;
    input.config.mode                = AlgorithmMode::direct;
    input.config.search.max_solutions = 1;   // CLI default — the regression trigger
    input.registry.init(std::move(infos),
                        {NSID("sharpness"), NSID("knockback")}, eq);
    input.data = DirectPayload{};   // empty source → resolver generates all books
    input.target.type = ItemType::Equip;
    input.target.enchs.insert(Ench{0, 5});
    input.target.enchs.insert(Ench{1, 2});

    ExecutionContext ctx(0, "penalty_balance");
    DynamicPenaltyBalancingAlgorithm algo;
    algo.init(input, ctx);
    algo.execute(input, ctx);

    // sharpness V + knockback II on a fresh sword needs several merge steps; the
    // greedy must run to completion even though max_solutions == 1.
    auto sols = ctx.get_solutions();
    expect(!sols.empty(),
           "multi-step target must complete even with max_solutions=1");
    expect(!sols[0].steps.empty(),
           "completed solution should contain merge steps");
    TEST_PASS("test_penalty_balance_multi_step_with_max_solutions_1");
}

int main() {
    try {
        test_penalty_balance_multi_step_with_max_solutions_1();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
