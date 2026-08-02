#include "framework/test_utils.h"
#include "domain/orchestration/orchestration.h"
#include "domain/business/types/Profile.h"
#include "domain/business/types/EquipmentTag.h"
#include "domain/business/components/TagResolver.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {

// Helper: create a test EnchantmentRegistry with sword-applicable enchants
EnchantmentRegistry make_sword_registry() {
    return EnchantmentRegistry({
        {NSID("sharpness"), "Sharpness", MCE::All, 5, 5,
         1, false, {}, {EquipmentTag::sword()}},
        {NSID("knockback"), "Knockback", MCE::All, 2, 2,
         2, false, {}, {EquipmentTag::sword()}},
        {NSID("protection"), "Protection", MCE::All, 4, 4,
         1, false, {}, {EquipmentTag::chestplate()}},
    });
}

// Helper: build a test Profile with sword data.
// Attaches a TagResolver so tag membership (diamond_sword ∈ #minecraft:sword)
// is known at the compact boundary.
Profile make_sword_profile() {
    Profile profile("test:compact");
    profile.add_tag({EquipmentTag::sword(), "sword"});
    profile.add_tag({EquipmentTag::chestplate(), "chestplate"});
    profile.add_equipment({NSID("minecraft:diamond_sword"), "Diamond Sword",
                           EquipmentTag::sword(), 1561});
    for (const auto& ench : make_sword_registry())
        profile.add_enchantment(ench);

    auto tr = std::make_shared<TagResolver>();
    tr->add_tag("minecraft:sword", {NSID("minecraft:diamond_sword").str()});
    profile.set_tag_resolver(std::move(tr));
    return profile;
}

// Helper: apply with the profile's attached TagResolver (all profiles in this
// test file attach one).
algorithm::AlgorithmInput apply_for(const Profile& profile, const SolveRequest& request) {
    return CompactAdapter::apply(profile, request, *profile.tag_resolver());
}

// Helper: create a SolveRequest for simple direct-mode tests
SolveRequest make_request(const Item& target_item, EnchSet source_enchs = {}) {
    SolveRequest request;
    request.target_item = target_item;
    request.mode = AlgorithmMode::direct;
    request.payload = DirectPayload{std::move(source_enchs)};
    request.forge_config = algorithm::ForgeConfig{};
    request.forge_config.platform = MCE::Java;
    request.search_config = algorithm::SearchConfig{};
    return request;
}

// ─── Test 1: minimal valid input produces correct AlgorithmInput ───
void test_apply_valid_input() {
    auto profile = make_sword_profile();
    // Direct mode requires a non-empty target enchant set.
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);
    auto request = make_request(target_item);
    auto input = apply_for(profile, request);

    expect(input.target.enchs.size() == 1, "target should have 1 enchantment");
    expect(input.is_direct(), "direct mode input");
    expect(input.registry.get_target_equip().max_durability == 1561,
           "equipment durability should be 1561");

    TEST_PASS("test_apply_valid_input");
}

// ─── Test 2: target enchantments are forwarded ───
void test_apply_with_target() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);
    auto request = make_request(target_item);
    auto input = apply_for(profile, request);

    expect(input.target.enchs.size() == 1, "target should have 1 enchantment");
    expect((*input.target.enchs.begin()).level() == 5, "target enchantment level should be 5");

    TEST_PASS("test_apply_with_target");
}

// ─── Test 3: unknown enchant ID is silently dropped (no throw) ───
void test_apply_invalid_enchant_id() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("minecraft:nonexistent"), "Nonexistent", 1);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);
    auto request = make_request(target_item);
    auto input = apply_for(profile, request);

    expect(input.target.enchs.empty(),
           "target should be empty (invalid ID silently dropped)");

    TEST_PASS("test_apply_invalid_enchant_id");
}

// ─── Test 4: target level exceeding max_level throws ───
//     (sharpness max_level is 5 in make_sword_registry)
void test_apply_level_exceeds_max_throws() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 99);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);
    auto request = make_request(target_item);

    expect_throws_as<std::runtime_error>(
        [&] { apply_for(profile, request); },
        "over-level target enchant should throw");

    TEST_PASS("test_apply_level_exceeds_max_throws");
}

// ─── Test 5: inapplicable enchant is excluded from EnchReg ───
void test_apply_inapplicable_enchant() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);
    auto request = make_request(target_item);
    auto input = apply_for(profile, request);

    // Global registry has 3 enchants; only 2 (sharpness, knockback) are
    // sword-applicable. protection is chestplate-only.
    expect(input.registry.size() == 2,
           "ench_reg should only have sword-applicable enchantments (2, not 3)");

    TEST_PASS("test_apply_inapplicable_enchant");
}

// ─── Test 6: prior_penalty is forwarded as-is ───
void test_apply_penalty_forward() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 32, 1561);
    auto request = make_request(target_item);
    auto input = apply_for(profile, request);

    expect(input.target.ppn == 32,
           "prior_penalty forwarded as-is (no overflow check)");

    TEST_PASS("test_apply_penalty_forward");
}

// ─── Test 7: ench_reg is pruned to only applicable enchantments ───
void test_pruning_only_applicable() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);
    auto request = make_request(target_item);
    auto input = apply_for(profile, request);

    // Global registry has 3 enchants; only 2 (sharpness, knockback) are sword-applicable
    expect(input.registry.size() == 2,
           "ench_reg should only have sword-applicable enchantments (2, not 3)");

    TEST_PASS("test_pruning_only_applicable");
}

// ─── Test 8: domain → compact preserves data ───
void test_from_domain() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);
    auto request = make_request(target_item);
    auto input = apply_for(profile, request);
    const auto& reg = input.registry;

    // Create a business Item with sharpness 5, prior_penalty 3
    EnchSet business_enchs;
    business_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item domain_item(NSID("minecraft:diamond_sword"), business_enchs, 3, 1561);

    // domain → compact
    auto compact_item = CompactAdapter::from_domain(domain_item, reg);

    expect(compact_item.type == algorithm::ItemType::Equip,
           "compact type should be Equip");
    expect(compact_item.ppn == 3, "compact prior_penalty should be 3");
    expect(compact_item.dur == 1561,
           "compact durability should match max");

    TEST_PASS("test_from_domain");
}

// ─── Test 9: recall returns empty for invalid output ───
void test_recall_empty_output() {
    algorithm::AlgorithmOutput output;
    output.is_valid = false;

    algorithm::AlgorithmInput input;
    input.config.forge.platform = MCE::Java;

    auto solutions = CompactAdapter::recall(output, input);
    expect(solutions.empty(),
           "recall() should return empty vector for is_valid=false");

    TEST_PASS("test_recall_empty_output");
}

// ─── Test 10: inapplicable target enchant throws (was silently dropped) ───
void test_apply_inapplicable_target_throws() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("protection"), "Protection", 4);  // chestplate-only
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);
    auto request = make_request(target_item);

    expect_throws_as<std::runtime_error>(
        [&] { apply_for(profile, request); },
        "inapplicable target enchant should throw");

    TEST_PASS("test_apply_inapplicable_target_throws");
}

// ─── Test 11: mixed applicable + inapplicable target enchants throw ───
void test_apply_mixed_target_throws() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 5);   // applicable
    target_enchs.emplace(NSID("protection"), "Protection", 4); // chestplate-only
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);
    auto request = make_request(target_item);

    expect_throws_as<std::runtime_error>(
        [&] { apply_for(profile, request); },
        "mixed inapplicable target enchant should throw");

    TEST_PASS("test_apply_mixed_target_throws");
}

// ─── Test 12: inapplicable source enchant throws (direct mode) ───
void test_apply_inapplicable_source_throws() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);

    EnchSet source_enchs;
    source_enchs.emplace(NSID("protection"), "Protection", 4);  // chestplate-only
    auto request = make_request(target_item, source_enchs);

    expect_throws_as<std::runtime_error>(
        [&] { apply_for(profile, request); },
        "inapplicable source enchant should throw");

    TEST_PASS("test_apply_inapplicable_source_throws");
}

// ─── Test 13: a book carrying an enchant inapplicable to the target still
//     works — books accept any enchantment (no applicability check) ───
void test_apply_inventory_inapplicable_extra_ok() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);

    SolveRequest request;
    request.target_item = target_item;
    request.mode = AlgorithmMode::inventory;
    InventoryPayload payload;
    EnchSet book_enchs;
    book_enchs.emplace(NSID("protection"), "Protection", 4);  // chestplate-only
    payload.extra_items.emplace_back(NSID("minecraft:enchanted_book"),
                                     book_enchs, 0);
    request.payload = std::move(payload);
    request.forge_config.platform = MCE::Java;
    request.search_config = algorithm::SearchConfig{};

    auto input = apply_for(profile, request);  // must not throw
    expect(!input.available().empty(),
           "inventory items should be present");

    TEST_PASS("test_apply_inventory_inapplicable_extra_ok");
}

// ─── Test 14: over-level source enchant throws (direct mode) ───
void test_apply_source_level_exceeds_max_throws() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);

    EnchSet source_enchs;
    source_enchs.emplace(NSID("sharpness"), "Sharpness", 99);  // max_level is 5
    auto request = make_request(target_item, source_enchs);

    expect_throws_as<std::runtime_error>(
        [&] { apply_for(profile, request); },
        "over-level source enchant should throw");

    TEST_PASS("test_apply_source_level_exceeds_max_throws");
}

// ─── Test 15: over-level enchant on inventory extra book throws ───
//     (inventory items are validated like direct mode: level ≤ max_level)
void test_apply_inventory_extra_over_level_throws() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);

    SolveRequest request;
    request.target_item = target_item;
    request.mode = AlgorithmMode::inventory;
    InventoryPayload payload;
    EnchSet book_enchs;
    book_enchs.emplace(NSID("sharpness"), "Sharpness", 99);  // max_level is 5
    payload.extra_items.emplace_back(NSID("minecraft:enchanted_book"),
                                     book_enchs, 0);
    request.payload = std::move(payload);
    request.forge_config.platform = MCE::Java;
    request.search_config = algorithm::SearchConfig{};

    expect_throws_as<std::runtime_error>(
        [&] { apply_for(profile, request); },
        "over-level inventory book should throw");

    TEST_PASS("test_apply_inventory_extra_over_level_throws");
}

// ─── Test 16: inventory mode forwards all items into available ───
void test_apply_inventory_equipment_split() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 5);  // goal
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);

    SolveRequest request;
    request.target_item = target_item;
    request.mode = AlgorithmMode::inventory;
    InventoryPayload payload;
    // Sacrifice book: sharpness III
    EnchSet book_enchs;
    book_enchs.emplace(NSID("sharpness"), "Sharpness", 3);
    payload.extra_items.emplace_back(NSID("minecraft:enchanted_book"),
                                     book_enchs, 0);
    // Another equipment entry: diamond_sword with sharpness II, ppn 3, dur 1000
    EnchSet cur_enchs;
    cur_enchs.emplace(NSID("sharpness"), "Sharpness", 2);
    payload.extra_items.emplace_back(NSID("minecraft:diamond_sword"),
                                     cur_enchs, 3, 1000);
    std::vector<int32_t> prios = {1, 2};
    request.payload = InventoryPayload{payload.extra_items, prios};
    request.forge_config.platform = MCE::Java;
    request.search_config = algorithm::SearchConfig{};

    auto input = apply_for(profile, request);

    // Inventory mode: available holds ALL items (book + equipment).  There is
    // no "current equipment" concept — the algorithm selects its own base.
    expect(input.is_inventory(), "inventory mode input");
    expect(input.available().size() == 2, "available holds both items");
    expect(input.inventory().priorities.size() == 2,
           "priorities parallel to available");
    expect(input.inventory().priorities[0] == 1, "book priority forwarded");
    TEST_PASS("test_apply_inventory_equipment_split");
}

// ─── Test 17: inventory equipment carrying an inapplicable enchant throws ───
void test_apply_inventory_equipment_inapplicable_throws() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);

    SolveRequest request;
    request.target_item = target_item;
    request.mode = AlgorithmMode::inventory;
    InventoryPayload payload;
    EnchSet eq_enchs;
    eq_enchs.emplace(NSID("protection"), "Protection", 4);  // chestplate-only
    // An inventory diamond_sword cannot carry protection.
    payload.extra_items.emplace_back(NSID("minecraft:diamond_sword"),
                                     eq_enchs, 0, 1561);
    request.payload = std::move(payload);
    request.forge_config.platform = MCE::Java;
    request.search_config = algorithm::SearchConfig{};

    expect_throws_as<std::runtime_error>(
        [&] { apply_for(profile, request); },
        "inventory equipment with inapplicable enchant should throw");

    TEST_PASS("test_apply_inventory_equipment_inapplicable_throws");
}

// ─── Test 18: inventory equipment with over-level enchant throws ───
void test_apply_inventory_equipment_over_level_throws() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item target_item(NSID("minecraft:diamond_sword"), target_enchs, 0, 1561);

    SolveRequest request;
    request.target_item = target_item;
    request.mode = AlgorithmMode::inventory;
    InventoryPayload payload;
    EnchSet eq_enchs;
    eq_enchs.emplace(NSID("sharpness"), "Sharpness", 99);  // max_level is 5
    payload.extra_items.emplace_back(NSID("minecraft:diamond_sword"),
                                     eq_enchs, 0, 1561);
    request.payload = std::move(payload);
    request.forge_config.platform = MCE::Java;
    request.search_config = algorithm::SearchConfig{};

    expect_throws_as<std::runtime_error>(
        [&] { apply_for(profile, request); },
        "inventory equipment with over-level enchant should throw");

    TEST_PASS("test_apply_inventory_equipment_over_level_throws");
}

// ─── Test 19: tag-intersection applicability ───
// The enchant supports the #minecraft:swords tag while the equipment's display
// category is #minecraft:sword.  Applicability is proven ONLY by the tag
// intersection: diamond_sword ∈ #minecraft:swords per the TagResolver — not by
// any category match.  Inventory mode (empty target enchants, book sacrifice)
// lets the assertion reach the registry content directly — direct mode would
// throw "ench_not_applicable" from the old logic before the assertion runs.
void test_apply_supported_items_tag_intersection() {
    Profile p("test:tagapp");
    p.add_equipment({NSID("minecraft:diamond_sword"), "Diamond Sword",
                     NSID("#minecraft:sword"), 1561});
    p.add_enchantment({NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5,
                       1, false, {}, {NSID("#minecraft:swords")}});

    TagResolver tr;
    tr.add_tag("minecraft:swords", {"minecraft:diamond_sword"});
    p.set_tag_resolver(std::make_shared<TagResolver>(tr));

    SolveRequest req;
    req.mode = AlgorithmMode::inventory;
    req.target_item = Item(NSID("minecraft:diamond_sword"), EnchSet{}, 0, 1561);
    EnchSet book_enchs;
    book_enchs.emplace(NSID("minecraft:sharpness"), "Sharpness", 5);
    InventoryPayload payload;
    payload.extra_items.emplace_back(NSID("minecraft:enchanted_book"), book_enchs, 0);
    req.payload = std::move(payload);
    req.forge_config = algorithm::ForgeConfig{};

    auto input = CompactAdapter::apply(p, req, *p.tag_resolver());

    bool sharpness_present = false;
    for (const auto& gid : input.registry.get_global_ids())
        if (gid == NSID("minecraft:sharpness")) sharpness_present = true;
    expect(sharpness_present,
           "sharpness applicable to diamond_sword via swords tag intersection");

    TEST_PASS("test_apply_supported_items_tag_intersection");
}

// ─── Test 19b: platform filter ───
// An enchantment restricted to one platform is excluded from a solve targeting
// the other, even when the tag intersection would admit it.
void test_apply_platform_filter() {
    Profile p("test:plat");
    p.add_equipment({NSID("minecraft:diamond_sword"), "Diamond Sword",
                     NSID("#minecraft:sword"), 1561});
    // sharpness is Java-only; a Bedrock solve must exclude it despite being
    // tag-applicable (diamond_sword ∈ #minecraft:swords).
    p.add_enchantment({NSID("minecraft:sharpness"), "Sharpness", MCE::Java, 5, 5,
                       1, false, {}, {NSID("#minecraft:swords")}});

    TagResolver tr;
    tr.add_tag("minecraft:swords", {"minecraft:diamond_sword"});
    p.set_tag_resolver(std::make_shared<TagResolver>(tr));

    SolveRequest req;
    req.mode = AlgorithmMode::inventory;
    req.target_item = Item(NSID("minecraft:diamond_sword"), EnchSet{}, 0, 1561);
    req.payload = InventoryPayload{};
    req.forge_config = algorithm::ForgeConfig{};
    req.forge_config.platform = MCE::Bedrock;

    auto input = CompactAdapter::apply(p, req, *p.tag_resolver());
    for (const auto& gid : input.registry.get_global_ids())
        expect(gid != NSID("minecraft:sharpness"),
               "Java-only sharpness excluded from Bedrock solve");
    TEST_PASS("test_apply_platform_filter");
}

// ─── Test: book target accepts every enchantment ───
// A book (→ enchanted_book) can hold enchantments from any category: neither
// sharpness (sword-only) nor protection (chestplate-only) is applicable to a
// book via the tag system, yet both must enter the compact registry.
void test_apply_book_target_all_enchants_applicable() {
    auto profile = make_sword_profile();
    EnchSet target_enchs;
    target_enchs.emplace(NSID("sharpness"), "Sharpness", 5);
    Item target_item(NSID("minecraft:enchanted_book"), target_enchs, 0, 0);
    auto request = make_request(target_item);
    auto input = apply_for(profile, request);

    expect(input.target.type == algorithm::ItemType::Book,
           "book target maps to Book item type");
    bool sharpness_present = false;
    bool protection_present = false;
    for (const auto& gid : input.registry.get_global_ids()) {
        if (gid == NSID("minecraft:sharpness")) sharpness_present = true;
        if (gid == NSID("minecraft:protection")) protection_present = true;
    }
    expect(sharpness_present,
           "sharpness applicable to a book target despite sword-only tag");
    expect(protection_present,
           "protection applicable to a book target despite chestplate-only tag");
    TEST_PASS("test_apply_book_target_all_enchants_applicable");
}

// ─── Test 20: inventory equipment carrying an enchant inapplicable to ITSELF
//     throws.  The validation must use the ITEM's own tag set, not the target's:
//     here the target is a chestplate (protection IS applicable there) while the
//     inventory equipment item is a sword (protection is NOT applicable).  If the
//     applicability were (wrongly) evaluated against the target's tags, this
//     would NOT throw — the test would fail. ───
void test_apply_inventory_equipment_inapplicable_ench() {
    Profile p("test:invbad");
    p.add_equipment({NSID("minecraft:diamond_sword"), "Diamond Sword",
                     NSID("#minecraft:sword"), 1561});
    p.add_equipment({NSID("minecraft:diamond_chestplate"), "Diamond Chestplate",
                     NSID("#minecraft:chestplate"), 528});
    // protection supports #minecraft:chestplate (NOT swords)
    p.add_enchantment({NSID("minecraft:protection"), "Protection", MCE::All, 4, 4,
                       1, false, {}, {NSID("#minecraft:chestplate")}});

    TagResolver tr;
    tr.add_tag("minecraft:sword", {"minecraft:diamond_sword"});
    tr.add_tag("minecraft:chestplate", {"minecraft:diamond_chestplate"});
    p.set_tag_resolver(std::make_shared<TagResolver>(tr));

    SolveRequest req;
    req.mode = AlgorithmMode::inventory;
    req.target_item = Item(NSID("minecraft:diamond_chestplate"), EnchSet{}, 0, 528);
    req.target_item.enchantments.emplace(NSID("minecraft:protection"), "Protection", 4);
    req.forge_config = algorithm::ForgeConfig{};

    // inventory: a diamond_sword EQUIPMENT item carrying protection (inapplicable to itself)
    std::vector<Item> items;
    EnchSet esc;
    esc.emplace(NSID("minecraft:protection"), "Protection", 4);
    items.push_back(Item(NSID("minecraft:diamond_sword"), esc, 0, 1561));
    req.payload = InventoryPayload{items, {}};

    bool threw = false;
    try { CompactAdapter::apply(p, req, *p.tag_resolver()); }
    catch (const std::runtime_error&) { threw = true; }
    expect(threw, "inventory equipment item with inapplicable enchant must throw");
    TEST_PASS("test_apply_inventory_equipment_inapplicable_ench");
}

} // anonymous namespace

int main() {
    try {
        test_apply_valid_input();
        test_apply_with_target();
        test_apply_invalid_enchant_id();
        test_apply_level_exceeds_max_throws();
        test_apply_inapplicable_enchant();
        test_apply_penalty_forward();
        test_pruning_only_applicable();
        test_from_domain();
        test_recall_empty_output();
        test_apply_inapplicable_target_throws();
        test_apply_mixed_target_throws();
        test_apply_inapplicable_source_throws();
        test_apply_inventory_inapplicable_extra_ok();
        test_apply_source_level_exceeds_max_throws();
        test_apply_inventory_extra_over_level_throws();
        test_apply_inventory_equipment_split();
        test_apply_inventory_equipment_inapplicable_throws();
        test_apply_inventory_equipment_over_level_throws();
        test_apply_supported_items_tag_intersection();
        test_apply_platform_filter();
        test_apply_book_target_all_enchants_applicable();
        test_apply_inventory_equipment_inapplicable_ench();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
