#define BESQ_TEST_MAIN
#include "framework/test_framework.h"
#include "diff_first/DiffFirstAlgorithm.h"
#include "domain/algorithm/ExecutionContext.h"
#include "domain/algorithm/types/AlgorithmTypes.h"
#include "domain/algorithm/types/ConfigTypes.h"
#include "domain/algorithm/types/Enchantment.h"
#include "domain/algorithm/types/Equipment.h"
#include "domain/algorithm/types/Item.h"
#include <memory>
#include <vector>
using namespace algorithm;

// ─── Regression: wasteful book-book merge orientation ────────────────────
//
// Direct mode, book target, source carries a conflicting enchant:
//   target = Enchanted Book[Sharpness III], source = Smite V.
// The resolver produces pool [book{smite5}, book{sharpness3}].  difficulty_first
// processes the PPN-0 tier by merging the two cheapest books, which selects
// base = book{smite5} (higher self-cost sorts first) → the sharpness book is
// forged INTO the smite base and ForgeEngine::forge_into drops the conflicting
// sharpness — the only source of the target enchant → false
// "Target unreachable".  Forging the reverse (smite INTO sharpness) keeps it.
// The reverse-orientation guard must swap the pair when the chosen direction
// wastes a target enchant and both items are books and the reverse doesn't.

TEST_CASE("test_diff_first_book_target_wasteful_merge_reversed") {
    std::vector<EnchInfo> infos(2);
    infos[0].id         = 0;  // sharpness
    infos[0].mul        = 1;
    infos[0].mul_b      = 1;
    infos[0].max_lvl    = 5;
    infos[0].exc_mask   = 0;
    infos[0].applicable = true;
    infos[1].id         = 1;  // smite — conflicts with sharpness
    infos[1].mul        = 1;
    infos[1].mul_b      = 1;
    infos[1].max_lvl    = 5;
    infos[1].applicable = true;
    // Conflict matrix builder ORs both directions, but declare on both sides
    // so is_conflict(sharpness, smite) is symmetric.
    infos[0].exc_mask |= (algorithm::mask_type{1} << 1);
    infos[1].exc_mask |= (algorithm::mask_type{1} << 0);

    Equipment eq;
    eq.id             = NSID("test");
    eq.max_durability = 1561;
    eq.applicable_enchs.insert(0);
    eq.applicable_enchs.insert(1);

    AlgorithmInput input;
    input.config.forge.platform = MCE::Java;
    input.config.mode           = AlgorithmMode::direct;
    input.registry.init(std::move(infos), {NSID("sharpness"), NSID("smite")}, eq);
    input.data = DirectPayload{EnchCollection{Ench{1, 5}}};  // source = smite V
    input.target.type = ItemType::Book;
    input.target.enchs.insert(Ench{0, 3});  // target = Enchanted Book[Sharpness III]

    ExecutionContext ctx(0, "difficulty_first");
    DiffFirstAlgorithm algo;
    algo.init(input, ctx);
    algo.execute(input, ctx);

    auto sols = ctx.get_solutions();
    expect(!sols.empty(), "book target must be reachable (1-step reverse forge)");
    expect(sols[0].steps.size() == 1, "expected a 1-step forge");
    const Item &result = sols[0].steps[0].result;
    expect(result.type == ItemType::Book, "final item must be a book");
    expect(result.enchs.contains(0) && result.enchs[0] == 3,
           "final item must carry Sharpness III");
    // The reverse orientation must actually have been forged: base = sharpness
    // book, sacrifice = smite book.
    expect(sols[0].steps[0].base.enchs.contains(0),
           "forge base must be the sharpness book");
    expect(sols[0].steps[0].sacrifice.enchs.contains(1),
           "forge sacrifice must be the smite book");
    TEST_PASS("test_diff_first_book_target_wasteful_merge_reversed");
}
