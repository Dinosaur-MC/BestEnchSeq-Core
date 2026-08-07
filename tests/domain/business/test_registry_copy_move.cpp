#define BESQ_TEST_MAIN
#include "framework/test_framework.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/registries/TagRegistry.h"
#include "domain/business/types/Enchantment.h"

#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>

// Compile-time sanity: all three registries must be copyable and movable.
static_assert(std::is_copy_constructible_v<EnchantmentRegistry>,
    "EnchantmentRegistry must be copy constructible");
static_assert(std::is_move_constructible_v<EnchantmentRegistry>,
    "EnchantmentRegistry must be move constructible");
static_assert(std::is_copy_assignable_v<EnchantmentRegistry>,
    "EnchantmentRegistry must be copy assignable");
static_assert(std::is_move_assignable_v<EnchantmentRegistry>,
    "EnchantmentRegistry must be move assignable");

static_assert(std::is_copy_constructible_v<EquipmentRegistry>,
    "EquipmentRegistry must be copy constructible");
static_assert(std::is_move_constructible_v<EquipmentRegistry>,
    "EquipmentRegistry must be move constructible");

static_assert(std::is_copy_constructible_v<TagRegistry>,
    "TagRegistry must be copy constructible");
static_assert(std::is_move_constructible_v<TagRegistry>,
    "TagRegistry must be move constructible");

// ═════════════════════════════════════════════════════════════════════════════
// Copy and move semantics tests for all three business-domain registries
//
// Verifies that compiler-generated copy/move constructors and assignment
// operators produce independent copies and that moved-from objects enter a
// valid-but-unspecified (empty) state.
// ═════════════════════════════════════════════════════════════════════════════

namespace {

// ============================================================================
// Helper factories
// ============================================================================

std::vector<EnchInfo> make_incompatible_enchants() {
    std::vector<EnchInfo> infos;
    // sharpness <-> smite are mutually exclusive
    infos.emplace_back(
        NSID("sharpness"), "Sharpness", MCE::All,
        5, 5, 1, false,
        std::unordered_set<NSID>{NSID("smite")},
        std::unordered_set<NSID>{}
    );
    infos.emplace_back(
        NSID("smite"), "Smite", MCE::All,
        5, 5, 1, false,
        std::unordered_set<NSID>{NSID("sharpness")},
        std::unordered_set<NSID>{}
    );
    // Unbreaking — compatible with everything
    infos.emplace_back(
        NSID("unbreaking"), "Unbreaking", MCE::All,
        3, 3, 1, false,
        std::unordered_set<NSID>{},
        std::unordered_set<NSID>{}
    );
    // Mending — treasure, compatible with everything
    infos.emplace_back(
        NSID("mending"), "Mending", MCE::All,
        1, 1, 2, true,
        std::unordered_set<NSID>{},
        std::unordered_set<NSID>{}
    );
    return infos;
}

std::vector<Equipment> make_equipment_list() {
    std::vector<Equipment> eqs;
    eqs.emplace_back(Equipment{
        NSID("minecraft:diamond_sword"), "Diamond Sword",
        EquipmentTag::sword(), 1561
    });
    eqs.emplace_back(Equipment{
        NSID("minecraft:diamond_pickaxe"), "Diamond Pickaxe",
        EquipmentTag::pickaxe(), 1561
    });
    eqs.emplace_back(Equipment{
        NSID("minecraft:iron_sword"), "Iron Sword",
        EquipmentTag::sword(), 250
    });
    return eqs;
}

std::vector<EquipmentTag> make_tag_list() {
    std::vector<EquipmentTag> tags;
    tags.emplace_back(EquipmentTag{EquipmentTag::sword(), "sword"});
    tags.emplace_back(EquipmentTag{EquipmentTag::helmet(), "helmet"});
    tags.emplace_back(EquipmentTag{EquipmentTag::pickaxe(), "pickaxe"});
    // Custom (non-builtin) tag
    tags.emplace_back(EquipmentTag{NSID("#minecraft:mace"), "mace"});
    return tags;
}

// ============================================================================
// EnchantmentRegistry
// ============================================================================

TEST_CASE("test_ench_copy_construction") {
    auto infos = make_incompatible_enchants();
    EnchantmentRegistry reg1(infos);

    // Copy construct
    EnchantmentRegistry reg2(reg1);

    // Both have the same size
    expect(reg1.size() == 4, "copy ctor: source size == 4");
    expect(reg2.size() == 4, "copy ctor: dest size == 4");

    // Both resolve is_incompatible identically
    expect(reg1.is_incompatible(NSID("sharpness"), NSID("smite")),
           "copy ctor: source sharpness/smite incompatible");
    expect(reg2.is_incompatible(NSID("sharpness"), NSID("smite")),
           "copy ctor: dest sharpness/smite incompatible");
    expect(!reg2.is_incompatible(NSID("sharpness"), NSID("unbreaking")),
           "copy ctor: dest sharpness/unbreaking compatible");
    expect(!reg2.is_incompatible(NSID("mending"), NSID("unbreaking")),
           "copy ctor: dest mending/unbreaking compatible");

    // Modify reg1 — erase one enchantment
    reg1.erase(NSID("smite"));
    expect(reg1.size() == 3, "copy ctor: source after erase == 3");
    expect(reg2.size() == 4, "copy ctor: dest unchanged after source erase");

    // reg2 must still have smite and its incompatibilities
    expect(reg2.contains(NSID("smite")),
           "copy ctor: dest still holds smite after source erase");
    expect(reg2.is_incompatible(NSID("sharpness"), NSID("smite")),
           "copy ctor: dest incompatibility preserved after source erase");

    // reg1 must no longer report sharpness/smite incompatibility
    expect(!reg1.is_incompatible(NSID("sharpness"), NSID("smite")),
           "copy ctor: source incompatibility gone after erase");

    // Modify reg2 — insert a new enchantment
    reg2.insert(EnchInfo{
        NSID("bane_of_arthropods"), "Bane of Arthropods", MCE::All,
        5, 5, 1, false,
        std::unordered_set<NSID>{},
        std::unordered_set<NSID>{}
    });
    expect(reg2.size() == 5, "copy ctor: dest after insert == 5");
    expect(reg1.size() == 3, "copy ctor: source unchanged after dest insert");

    std::cout << "PASS: test_ench_copy_construction" << std::endl;
}

TEST_CASE("test_ench_copy_assignment") {
    auto infos = make_incompatible_enchants();
    EnchantmentRegistry reg1(infos);
    EnchantmentRegistry reg2;

    // Copy assign
    reg2 = reg1;

    expect(reg1.size() == 4, "copy assign: source size == 4");
    expect(reg2.size() == 4, "copy assign: dest size == 4");

    // Independence: erase from reg1
    reg1.erase(NSID("sharpness"));
    expect(reg1.size() == 3, "copy assign: source after erase == 3");
    expect(reg2.size() == 4, "copy assign: dest unchanged after source erase");
    expect(reg2.contains(NSID("sharpness")),
           "copy assign: dest still has sharpness");
    expect(reg2.is_incompatible(NSID("sharpness"), NSID("smite")),
           "copy assign: dest incompatibility preserved");

    std::cout << "PASS: test_ench_copy_assignment" << std::endl;
}

TEST_CASE("test_ench_move_construction") {
    auto infos = make_incompatible_enchants();
    EnchantmentRegistry reg1(infos);

    // Verify source state before move
    expect(reg1.size() == 4, "move ctor: before-move size == 4");
    expect(reg1.is_incompatible(NSID("sharpness"), NSID("smite")),
           "move ctor: before-move incompatibility works");

    // Move construct
    EnchantmentRegistry reg2(std::move(reg1));

    // Source is in valid-but-unspecified state (empty)
    expect(reg1.size() == 0, "move ctor: moved-from source size == 0");

    // Dest has correct data
    expect(reg2.size() == 4, "move ctor: dest size == 4");
    expect(reg2.contains(NSID("sharpness")), "move ctor: dest has sharpness");
    expect(reg2.contains(NSID("smite")), "move ctor: dest has smite");
    expect(reg2.contains(NSID("unbreaking")), "move ctor: dest has unbreaking");
    expect(reg2.contains(NSID("mending")), "move ctor: dest has mending");

    // Incompatibility preserved on dest
    expect(reg2.is_incompatible(NSID("sharpness"), NSID("smite")),
           "move ctor: dest sharpness/smite incompatible");
    expect(!reg2.is_incompatible(NSID("smite"), NSID("unbreaking")),
           "move ctor: dest smite/unbreaking compatible");

    // Moved-from object is safely destructible and re-usable
    reg1 = EnchantmentRegistry(); // re-assign to empty — no crash
    expect(reg1.size() == 0, "move ctor: re-assigned moved-from source is empty");

    std::cout << "PASS: test_ench_move_construction" << std::endl;
}

TEST_CASE("test_ench_move_assignment") {
    auto infos = make_incompatible_enchants();
    EnchantmentRegistry reg1(infos);
    EnchantmentRegistry reg2;

    // Move assign
    reg2 = std::move(reg1);

    expect(reg1.size() == 0, "move assign: moved-from source size == 0");
    expect(reg2.size() == 4, "move assign: dest size == 4");
    expect(reg2.is_incompatible(NSID("sharpness"), NSID("smite")),
           "move assign: dest incompatibility preserved");
    expect(!reg2.is_incompatible(NSID("sharpness"), NSID("unbreaking")),
           "move assign: dest compatibility preserved");

    // Safe to destroy both
    std::cout << "PASS: test_ench_move_assignment" << std::endl;
}

TEST_CASE("test_ench_self_assignment") {
    auto infos = make_incompatible_enchants();
    EnchantmentRegistry reg(infos);

    // Self-copy-assignment must be a no-op
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
    reg = reg;
#pragma clang diagnostic pop

    expect(reg.size() == 4, "self assign: size unchanged");
    expect(reg.contains(NSID("sharpness")), "self assign: sharpness present");
    expect(reg.contains(NSID("smite")), "self assign: smite present");
    expect(reg.contains(NSID("unbreaking")), "self assign: unbreaking present");
    expect(reg.contains(NSID("mending")), "self assign: mending present");
    expect(reg.is_incompatible(NSID("sharpness"), NSID("smite")),
           "self assign: incompatibility preserved");
    expect(!reg.is_incompatible(NSID("sharpness"), NSID("unbreaking")),
           "self assign: compatibility preserved");

    std::cout << "PASS: test_ench_self_assignment" << std::endl;
}

// ============================================================================
// EquipmentRegistry
// ============================================================================

TEST_CASE("test_eq_copy_construction") {
    auto eqs = make_equipment_list();
    EquipmentRegistry reg1(eqs);

    // Copy construct
    EquipmentRegistry reg2(reg1);

    expect(reg1.size() == 3, "eq copy ctor: source size == 3");
    expect(reg2.size() == 3, "eq copy ctor: dest size == 3");

    // Verify lookup on copy
    const auto& e0 = reg2.at(NSID("minecraft:diamond_sword"));
    expect(e0.name == "Diamond Sword", "eq copy ctor: dest diamond_sword name");
    expect(e0.max_durability == 1561, "eq copy ctor: dest diamond_sword durability");

    // Verify get_by_category on copy
    auto swords = reg2.get_by_category(EquipmentTag::sword());
    expect(swords.size() == 2, "eq copy ctor: dest has 2 swords by category");
    auto pickaxes = reg2.get_by_category(EquipmentTag::pickaxe());
    expect(pickaxes.size() == 1, "eq copy ctor: dest has 1 pickaxe by category");

    // Independence: erase from reg1
    reg1.erase(NSID("minecraft:iron_sword"));
    expect(reg1.size() == 2, "eq copy ctor: source after erase == 2");
    expect(reg2.size() == 3, "eq copy ctor: dest unchanged after source erase");
    expect(reg2.contains(NSID("minecraft:iron_sword")),
           "eq copy ctor: dest still has iron_sword");

    std::cout << "PASS: test_eq_copy_construction" << std::endl;
}

TEST_CASE("test_eq_move_construction") {
    auto eqs = make_equipment_list();
    EquipmentRegistry reg1(eqs);

    // Move construct
    EquipmentRegistry reg2(std::move(reg1));

    expect(reg1.size() == 0, "eq move ctor: moved-from source size == 0");
    expect(reg2.size() == 3, "eq move ctor: dest size == 3");
    expect(reg2.contains(NSID("minecraft:diamond_sword")),
           "eq move ctor: dest has diamond_sword");
    expect(reg2.contains(NSID("minecraft:diamond_pickaxe")),
           "eq move ctor: dest has diamond_pickaxe");
    expect(reg2.contains(NSID("minecraft:iron_sword")),
           "eq move ctor: dest has iron_sword");

    // get_by_category still works
    auto swords = reg2.get_by_category(EquipmentTag::sword());
    expect(swords.size() == 2, "eq move ctor: dest swords by category == 2");

    std::cout << "PASS: test_eq_move_construction" << std::endl;
}

// ============================================================================
// TagRegistry
// ============================================================================

TEST_CASE("test_tag_copy") {
    auto tags = make_tag_list();
    TagRegistry reg1(tags);

    // Copy construct
    TagRegistry reg2(reg1);

    expect(reg1.size() == 4, "tag copy: source size == 4");
    expect(reg2.size() == 4, "tag copy: dest size == 4");

    // Custom tags survive in copy
    expect(reg2.contains(NSID("#minecraft:mace")),
           "tag copy: dest has custom tag mace");
    expect(!reg2.contains(NSID("#minecraft:wand")),
           "tag copy: dest does not have non-existent wand");

    // Builtin tags survive in copy
    expect(reg2.contains(NSID("#minecraft:sword")),
           "tag copy: dest has builtin tag sword");
    expect(reg2.contains(NSID("#minecraft:helmet")),
           "tag copy: dest has builtin tag helmet");

    // Convenience get() works on copy
    const auto& t0 = reg2.get("sword");
    expect(t0.name == "sword", "tag copy: dest get(\"sword\") name matches");
    const auto& t1 = reg2.get("mace");
    expect(t1.name == "mace", "tag copy: dest get(\"mace\") name matches");

    // Non-existent get() throws
    bool threw = false;
    try {
        reg2.get("nonexistent_tag");
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "tag copy: dest get(\"nonexistent\") throws out_of_range");

    // Independence: erase from reg1
    reg1.erase(NSID("#minecraft:mace"));
    expect(reg1.size() == 3, "tag copy: source after erase == 3");
    expect(reg2.size() == 4, "tag copy: dest unchanged after source erase");
    expect(reg2.contains(NSID("#minecraft:mace")),
           "tag copy: dest still has custom mace after source erase");

    std::cout << "PASS: test_tag_copy" << std::endl;
}

TEST_CASE("test_tag_move") {
    auto tags = make_tag_list();
    TagRegistry reg1(tags);

    // Move construct
    TagRegistry reg2(std::move(reg1));

    expect(reg1.size() == 0, "tag move: moved-from source size == 0");
    expect(reg2.size() == 4, "tag move: dest size == 4");
    expect(reg2.contains(NSID("#minecraft:sword")),
           "tag move: dest has builtin sword");
    expect(reg2.contains(NSID("#minecraft:helmet")),
           "tag move: dest has builtin helmet");
    expect(reg2.contains(NSID("#minecraft:mace")),
           "tag move: dest has custom mace");

    // get() works on dest
    const auto& sword_tag = reg2.get("sword");
    expect(sword_tag.name == "sword", "tag move: dest get(\"sword\") works");
    const auto& mace_tag = reg2.get("mace");
    expect(mace_tag.name == "mace", "tag move: dest get(\"mace\") works");

    // Non-existent get() still throws on dest
    bool threw = false;
    try {
        reg2.get("nonexistent_tag");
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "tag move: dest get(\"nonexistent\") throws out_of_range");

    std::cout << "PASS: test_tag_move" << std::endl;
}

} // anonymous namespace

// ============================================================================
// main
// ============================================================================
