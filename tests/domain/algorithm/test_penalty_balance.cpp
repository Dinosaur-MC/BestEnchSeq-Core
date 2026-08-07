#define BESQ_TEST_MAIN
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Equipment.h"
#include "domain/algorithm/types/Item.h"
#include "framework/test_framework.h"
#include "penalty_balance/DynamicPenaltyBalancingAlgorithm.h"
#include <vector>
using namespace algorithm;

// ─── Regression: penalty_balance misread max_solutions as a merge-STEP cap ──
//
// `config.search.max_solutions` is a SOLUTION-count cap (dfs/astar semantics:
// stop after N solutions found).  This greedy emits exactly one solution, so it
// must ignore it.  Before the fix (issue #17) the loop broke after
// `max_solutions` merge STEPS, so with the CLI default --solutions 1 any target
// needing ≥2 merges reported CompleteNoSolution and the target was "unreachable".

TEST_CASE("test_penalty_balance_multi_step_with_max_solutions_1") {
    std::vector<EnchInfo> infos(2);
    infos[0].id = 0;
    infos[0].mul = 1;
    infos[0].mul_b = 1;
    infos[0].max_lvl = 5;
    infos[0].exc_mask = 0;
    infos[0].applicable = true;
    infos[1].id = 1;
    infos[1].mul = 2;
    infos[1].mul_b = 1;
    infos[1].max_lvl = 2;
    infos[1].exc_mask = 0;
    infos[1].applicable = true;

    Equipment eq;
    eq.id = NSID("test");
    eq.max_durability = 1561;
    eq.applicable_enchs.insert(0);
    eq.applicable_enchs.insert(1);

    AlgorithmInput input;
    input.config.forge.platform = MCE::Java;
    input.config.mode = AlgorithmMode::direct;
    input.config.search.max_solutions = 1; // CLI default — the regression trigger
    input.registry.init(std::move(infos), {NSID("sharpness"), NSID("knockback")}, eq);
    input.data = DirectPayload{}; // empty source → resolver generates all books
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
    expect(!sols.empty(), "multi-step target must complete even with max_solutions=1");
    expect(!sols[0].steps.empty(), "completed solution should contain merge steps");
    TEST_PASS("test_penalty_balance_multi_step_with_max_solutions_1");
}

// ─── Regression: wasteful book-book merge orientation ────────────────────
//
// Direct mode, book target, source carries a conflicting enchant:
//   target = Enchanted Book[Sharpness III], source = Smite V.
// The resolver produces pool [book{smite5}, book{sharpness3}].  The greedy
// picks the forge pair by (pen_diff, est cost) → (smite, sharpness): it forges
// the sharpness book INTO the smite base, and ForgeEngine::forge_into drops
// the conflicting sharpness — the only source of the target enchant → false
// "Target unreachable".  Forging the reverse (smite INTO sharpness) keeps it.
// The reverse-orientation guard must swap the pair when the chosen direction
// wastes a target enchant and both items are books and the reverse doesn't.

TEST_CASE("test_penalty_balance_book_target_wasteful_merge_reversed") {
    std::vector<EnchInfo> infos(2);
    infos[0].id = 0; // sharpness
    infos[0].mul = 1;
    infos[0].mul_b = 1;
    infos[0].max_lvl = 5;
    infos[0].exc_mask = 0;
    infos[0].applicable = true;
    infos[1].id = 1; // smite — conflicts with sharpness
    infos[1].mul = 1;
    infos[1].mul_b = 1;
    infos[1].max_lvl = 5;
    infos[1].applicable = true;
    // Conflict matrix builder ORs both directions, but declare on both sides
    // so is_conflict(sharpness, smite) is symmetric.
    infos[0].exc_mask |= (algorithm::mask_type{1} << 1);
    infos[1].exc_mask |= (algorithm::mask_type{1} << 0);

    Equipment eq;
    eq.id = NSID("test");
    eq.max_durability = 1561;
    eq.applicable_enchs.insert(0);
    eq.applicable_enchs.insert(1);

    AlgorithmInput input;
    input.config.forge.platform = MCE::Java;
    input.config.mode = AlgorithmMode::direct;
    input.registry.init(std::move(infos), {NSID("sharpness"), NSID("smite")}, eq);
    input.data = DirectPayload{EnchCollection{Ench{1, 5}}}; // source = smite V
    input.target.type = ItemType::Book;
    input.target.enchs.insert(Ench{0, 3}); // target = Enchanted Book[Sharpness III]

    ExecutionContext ctx(0, "penalty_balance");
    DynamicPenaltyBalancingAlgorithm algo;
    algo.init(input, ctx);
    algo.execute(input, ctx);

    auto sols = ctx.get_solutions();
    expect(!sols.empty(), "book target must be reachable (1-step reverse forge)");
    expect(sols[0].steps.size() == 1, "expected a 1-step forge");
    const Item& result = sols[0].steps[0].result;
    expect(result.type == ItemType::Book, "final item must be a book");
    expect(result.enchs.contains(0) && result.enchs[0] == 3, "final item must carry Sharpness III");
    // The reverse orientation must actually have been forged: base = sharpness
    // book, sacrifice = smite book.
    expect(sols[0].steps[0].base.enchs.contains(0), "forge base must be the sharpness book");
    expect(sols[0].steps[0].sacrifice.enchs.contains(1), "forge sacrifice must be the smite book");
    TEST_PASS("test_penalty_balance_book_target_wasteful_merge_reversed");
}
