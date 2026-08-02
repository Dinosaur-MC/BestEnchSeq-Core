#include "framework/test_utils.h"
#include "domain/business/loaders/ProfileLoader.h"
#include "domain/business/loaders/RegistryLoader.h"
#include "domain/business/components/TagResolver.h"
#include <filesystem>
#include <fstream>
#include "domain/business/components/Serializer.h"
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/business/registries/TagRegistry.h"
#include "domain/business/types/dto/EnchantmentData.h"
#include "domain/business/types/dto/EquipmentData.h"
#include "domain/business/types/EnchInfo.h"
#include "domain/business/types/Equipment.h"
#include "domain/business/types/EquipmentTag.h"
#include "common/CommonTypes.h"
#include "common/io/json.h"

#include <string>
#include <unordered_set>
#include <vector>
#include <iostream>

namespace {

// =============================================================================
// Section A -- RegistryLoader
// =============================================================================

// ---------------------------------------------------------------------------
// 1. DTO to EnchantmentRegistry: verify size, contains, incompatibility,
//    exclusive_set, applicable_to, and field mapping.
// ---------------------------------------------------------------------------
void test_loader_ench_dto_to_reg() {
    // -- Prepare TagRegistry with known categories ---------------
    TagRegistry tag_reg;
    tag_reg.insert({NSID("#minecraft:sword"), "sword"});
    tag_reg.insert({NSID("#minecraft:helmet"), "helmet"});

    // -- Build EnchantmentData DTOs ---------------------------------------
    // Sharpness and Smite are mutually exclusive, both apply to swords.
    // Protection applies to helmets, no exclusives.
    std::vector<business::loader::EnchantmentData> data;

    data.push_back({
        "minecraft:sharpness",               // id
        "Sharpness",                         // display_name
        1,                                   // multiplier
        5,                                   // max_level
        5,                                   // limited_level (non-zero => not treasure)
        {"smite"},                           // exclusive_with (bare name, no namespace)
        {"#minecraft:sword"}                 // applicable_to (raw tag reference)
    });

    data.push_back({
        "minecraft:smite",
        "Smite",
        1,
        5,
        5,
        {"sharpness"},
        {"#minecraft:sword"}
    });

    data.push_back({
        "minecraft:protection",
        "Protection",
        1,
        4,
        4,
        {},
        {"#minecraft:helmet"}
    });

    // -- Convert via RegistryLoader ---------------------------------------
    RegistryLoader loader;
    EnchantmentRegistry ench_reg;
    EquipmentRegistry eq_reg;   // empty: only #tag references are used here
    loader.from_dto(ench_reg, tag_reg, eq_reg, data);

    // -- Verify basic structure -------------------------------------------
    expect(ench_reg.size() == 3, "ench_reg should have 3 enchantments");
    expect(ench_reg.contains(NSID("minecraft:sharpness")),   "contains sharpness");
    expect(ench_reg.contains(NSID("minecraft:smite")),       "contains smite");
    expect(ench_reg.contains(NSID("minecraft:protection")),  "contains protection");

    // -- Field mapping ----------------------------------------------------
    const auto& sharp = ench_reg.at(NSID("minecraft:sharpness"));
    expect(sharp.multiplier    == 1, "sharpness multiplier");
    expect(sharp.max_level     == 5, "sharpness max_level");
    expect(sharp.limited_level == 5, "sharpness limited_level");
    expect(!sharp.is_treasure,       "sharpness is_treasure = false (limited_level != 0)");

    // -- Verify incompatibility (bidirectional) ---------------------------
    // Sharpness <-> Smite are mutually exclusive
    expect(ench_reg.is_incompatible(
        NSID("minecraft:sharpness"), NSID("minecraft:smite")),
        "sharpness and smite are incompatible");
    expect(ench_reg.is_incompatible(
        NSID("minecraft:smite"), NSID("minecraft:sharpness")),
        "smite and sharpness are incompatible (symmetric)");

    // Protection has no exclusives -- compatible with both
    expect(!ench_reg.is_incompatible(
        NSID("minecraft:protection"), NSID("minecraft:sharpness")),
        "protection compatible with sharpness");
    expect(!ench_reg.is_incompatible(
        NSID("minecraft:protection"), NSID("minecraft:smite")),
        "protection compatible with smite");

    // Same enchantment is never incompatible with itself
    expect(!ench_reg.is_incompatible(
        NSID("minecraft:sharpness"), NSID("minecraft:sharpness")),
        "same ench never incompatible");

    // -- Verify exclusive_set ---------------------------------------------
    {
        const auto& excl = ench_reg.get_exclusive_set(NSID("minecraft:sharpness"));
        expect(excl.size() == 1, "sharpness exclusive_set size is 1");
        expect(excl.contains(NSID("minecraft:smite")),
               "sharpness exclusive_set contains minecraft:smite");
    }
    {
        const auto& excl = ench_reg.get_exclusive_set(NSID("minecraft:protection"));
        expect(excl.empty(), "protection exclusive_set is empty");
    }

    // -- Verify applicable_to resolution (tag NSIDs) ----------------------
    expect(sharp.supported_items.size() == 1,
           "sharpness applicable to 1 category");
    expect(sharp.supported_items.contains(NSID("#minecraft:sword")),
           "sharpness applicable to #minecraft:sword");

    const auto& prot = ench_reg.at(NSID("minecraft:protection"));
    expect(prot.supported_items.size() == 1,
           "protection applicable to 1 category");
    expect(prot.supported_items.contains(NSID("#minecraft:helmet")),
           "protection applicable to #minecraft:helmet");

    std::cout << "PASS: test_loader_ench_dto_to_reg" << std::endl;
}

// ---------------------------------------------------------------------------
// 2. DTO to EquipmentRegistry: verify size, contains, category resolution.
// ---------------------------------------------------------------------------
void test_loader_eq_dto_to_reg() {
    // -- Prepare TagRegistry -------------------------------------
    TagRegistry tag_reg;
    tag_reg.insert({NSID("#minecraft:sword"),    "sword"});
    tag_reg.insert({NSID("#minecraft:pickaxe"),  "pickaxe"});
    tag_reg.insert({NSID("#minecraft:helmet"),   "helmet"});

    // -- Build EquipmentData DTOs -----------------------------------------
    std::vector<business::loader::EquipmentData> data;
    data.push_back({"minecraft:diamond_sword",   "Diamond Sword",   "sword",   1561});
    data.push_back({"minecraft:iron_sword",      "Iron Sword",      "sword",   250});
    data.push_back({"minecraft:diamond_pickaxe", "Diamond Pickaxe", "pickaxe", 1561});
    data.push_back({"minecraft:diamond_helmet",  "Diamond Helmet",  "helmet",  363});

    // -- Convert via RegistryLoader ---------------------------------------
    RegistryLoader loader;
    EquipmentRegistry eq_reg;
    loader.from_dto(eq_reg, tag_reg, data);

    // -- Verify basic structure -------------------------------------------
    expect(eq_reg.size() == 4, "eq_reg should have 4 equipment entries");
    expect(eq_reg.contains(NSID("minecraft:diamond_sword")),   "contains diamond_sword");
    expect(eq_reg.contains(NSID("minecraft:iron_sword")),      "contains iron_sword");
    expect(eq_reg.contains(NSID("minecraft:diamond_pickaxe")), "contains diamond_pickaxe");
    expect(eq_reg.contains(NSID("minecraft:diamond_helmet")),  "contains diamond_helmet");

    // -- Verify fields and category resolution ----------------------------
    {
        const auto& ds = eq_reg.at(NSID("minecraft:diamond_sword"));
        expect(ds.name == "Diamond Sword",         "diamond_sword name preserved");
        expect(ds.category == EquipmentTag::sword(), "diamond_sword category = sword");
        expect(ds.max_durability == 1561,           "diamond_sword durability");
    }
    {
        const auto& ip = eq_reg.at(NSID("minecraft:iron_sword"));
        expect(ip.name == "Iron Sword",             "iron_sword name preserved");
        expect(ip.category == EquipmentTag::sword(), "iron_sword category = sword");
        expect(ip.max_durability == 250,            "iron_sword durability");
    }
    {
        const auto& dp = eq_reg.at(NSID("minecraft:diamond_pickaxe"));
        expect(dp.name == "Diamond Pickaxe",           "diamond_pickaxe name preserved");
        expect(dp.category == EquipmentTag::pickaxe(), "diamond_pickaxe category = pickaxe");
        expect(dp.max_durability == 1561,              "diamond_pickaxe durability");
    }
    {
        const auto& dh = eq_reg.at(NSID("minecraft:diamond_helmet"));
        expect(dh.name == "Diamond Helmet",           "diamond_helmet name preserved");
        expect(dh.category == EquipmentTag::helmet(), "diamond_helmet category = helmet");
        expect(dh.max_durability == 363,              "diamond_helmet durability");
    }

    std::cout << "PASS: test_loader_eq_dto_to_reg" << std::endl;
}

// ---------------------------------------------------------------------------
// 3. JSON roundtrip: registry -> Json -> registry, then verify contents.
// ---------------------------------------------------------------------------
void test_loader_json_roundtrip() {
    // -- Build original registries ----------------------------------------
    EnchantmentRegistry orig_ench;
    orig_ench.insert({
        NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
        std::unordered_set<NSID>{NSID("minecraft:smite")},
        std::unordered_set<NSID>{}
    });
    orig_ench.insert({
        NSID("minecraft:smite"), "Smite", MCE::All, 5, 5, 1, false,
        std::unordered_set<NSID>{},
        std::unordered_set<NSID>{}
    });

    EquipmentRegistry orig_eq;
    orig_eq.insert({
        NSID("minecraft:diamond_sword"), "Diamond Sword",
        NSID("#minecraft:sword"), 1561
    });

    TagRegistry orig_tags;
    orig_tags.insert({NSID("#minecraft:sword"), "sword"});

    // -- to_json (registry -> Json) ---------------------------------------
    RegistryLoader loader;
    Json ench_json = loader.to_json(orig_ench);
    Json eq_json   = loader.to_json(orig_eq);
    Json tag_json  = loader.to_json(orig_tags);

    expect(ench_json.is_valid(), "ench_json is valid");
    expect(eq_json.is_valid(),   "eq_json is valid");
    expect(tag_json.is_valid(),  "tag_json is valid");

    // -- from_json (Json -> new registry) ---------------------------------
    EnchantmentRegistry new_ench;
    EquipmentRegistry new_eq;
    TagRegistry new_tags;

    expect(loader.from_json(new_ench, ench_json), "from_json(ench) returns true");
    expect(loader.from_json(new_eq,   eq_json),   "from_json(eq) returns true");
    expect(loader.from_json(new_tags, tag_json),  "from_json(tags) returns true");

    // -- Verify EnchantmentRegistry contents ------------------------------
    expect(new_ench.size() == orig_ench.size(), "ench sizes match after roundtrip");
    expect(new_ench.contains(NSID("minecraft:sharpness")), "new ench has sharpness");
    expect(new_ench.contains(NSID("minecraft:smite")),     "new ench has smite");

    const auto& s = new_ench.at(NSID("minecraft:sharpness"));
    expect(s.max_level == 5,      "roundtrip sharpness max_level");
    expect(s.multiplier == 1,      "roundtrip sharpness multiplier");
    expect(s.name == "Sharpness",  "roundtrip sharpness name");

    // Incompatibility should be preserved
    expect(new_ench.is_incompatible(
        NSID("minecraft:sharpness"), NSID("minecraft:smite")),
        "roundtrip preserves sharpness <-> smite incompatibility");

    // -- Verify EquipmentRegistry contents --------------------------------
    expect(new_eq.size() == orig_eq.size(), "eq sizes match after roundtrip");
    expect(new_eq.contains(NSID("minecraft:diamond_sword")), "new eq has diamond_sword");

    const auto& restored_eq = new_eq.at(NSID("minecraft:diamond_sword"));
    expect(restored_eq.name == "Diamond Sword",      "roundtrip eq name");
    expect(restored_eq.max_durability == 1561,        "roundtrip eq durability");
    expect(restored_eq.category == NSID("#minecraft:sword"),
           "roundtrip eq category");

    // -- Verify TagRegistry contents -----------------------------
    expect(new_tags.size() == orig_tags.size(), "tag sizes match after roundtrip");
    expect(new_tags.contains(NSID("#minecraft:sword")), "new tags has sword tag");

    // -- Verify that from_json with invalid JSON returns false ------------
    EnchantmentRegistry empty_ench;
    Json bad_json = Json::null();
    expect(!loader.from_json(empty_ench, bad_json),
           "from_json with null JSON returns false");

    std::cout << "PASS: test_loader_json_roundtrip" << std::endl;
}

// ---------------------------------------------------------------------------
// 4. Full resolve pipeline: DTOs -> all three registries.
// ---------------------------------------------------------------------------
void test_loader_resolve_full() {
    // -- Build Equipment DTOs (two categories: sword, helmet) ------------
    std::vector<business::loader::EquipmentData> eq_data;
    eq_data.push_back({"minecraft:diamond_sword",  "Diamond Sword",  "sword",  1561});
    eq_data.push_back({"minecraft:iron_sword",     "Iron Sword",     "sword",  250});
    eq_data.push_back({"minecraft:diamond_helmet", "Diamond Helmet", "helmet", 363});

    // -- Build Enchantment DTOs -------------------------------------------
    std::vector<business::loader::EnchantmentData> ench_data;
    ench_data.push_back({
        "minecraft:sharpness", "Sharpness", 1, 5, 5,
        {"smite"}, {"#minecraft:sword"}
    });
    ench_data.push_back({
        "minecraft:smite", "Smite", 1, 5, 5,
        {"sharpness"}, {"#minecraft:sword"}
    });
    ench_data.push_back({
        "minecraft:protection", "Protection", 1, 4, 4,
        {}, {"#minecraft:helmet"}
    });
    ench_data.push_back({
        "minecraft:unbreaking", "Unbreaking", 1, 3, 3,
        {}, {"#minecraft:sword", "#minecraft:helmet"}
    });

    // -- Resolve via RegistryLoader ---------------------------------------
    RegistryLoader loader;
    TagRegistry tag_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;

    // The tag universe is provided as base_tags (vanilla fallback); resolve()
    // no longer synthesizes tags from the equipment data.
    TagRegistry base_tags;
    base_tags.insert({NSID("#minecraft:sword"), "sword"});
    base_tags.insert({NSID("#minecraft:helmet"), "helmet"});

    loader.resolve(ench_data, eq_data, tag_reg, eq_reg, ench_reg, &base_tags);

    // ---- Step 1: TagRegistry ----------------------------------
    // Seeded from base_tags only: 2 tags, sword + helmet.
    expect(tag_reg.size() == 2, "tag_reg should have 2 tags (sword, helmet)");
    expect(tag_reg.contains(NSID("#minecraft:sword")),
           "tag_reg has #minecraft:sword");
    expect(tag_reg.contains(NSID("#minecraft:helmet")),
           "tag_reg has #minecraft:helmet");

    // ---- Step 2: EquipmentRegistry -------------------------------------
    expect(eq_reg.size() == 3, "eq_reg should have 3 equipment entries");
    expect(eq_reg.contains(NSID("minecraft:diamond_sword")),  "eq_reg has diamond_sword");
    expect(eq_reg.contains(NSID("minecraft:iron_sword")),     "eq_reg has iron_sword");
    expect(eq_reg.contains(NSID("minecraft:diamond_helmet")), "eq_reg has diamond_helmet");

    // Category resolution: string "helmet" -> NSID("#minecraft:helmet")
    {
        const auto& helm = eq_reg.at(NSID("minecraft:diamond_helmet"));
        expect(helm.category == EquipmentTag::helmet(),
               "diamond_helmet category resolved to #minecraft:helmet");
    }

    // ---- Step 3: EnchantmentRegistry -----------------------------------
    expect(ench_reg.size() == 4, "ench_reg should have 4 enchantments");
    expect(ench_reg.contains(NSID("minecraft:sharpness")),  "ench_reg has sharpness");
    expect(ench_reg.contains(NSID("minecraft:protection")), "ench_reg has protection");
    expect(ench_reg.contains(NSID("minecraft:unbreaking")), "ench_reg has unbreaking");

    // Incompatibility: sharpness <-> smite (bidirectional)
    expect(ench_reg.is_incompatible(
        NSID("minecraft:sharpness"), NSID("minecraft:smite")),
        "sharpness incompatible with smite");
    expect(ench_reg.is_incompatible(
        NSID("minecraft:smite"), NSID("minecraft:sharpness")),
        "smite incompatible with sharpness (symmetric)");

    // Protection and sharpness are in different categories and have no exclusives
    expect(!ench_reg.is_incompatible(
        NSID("minecraft:protection"), NSID("minecraft:sharpness")),
        "protection compatible with sharpness");

    // Unbreaking has no exclusives -- compatible with all
    expect(!ench_reg.is_incompatible(
        NSID("minecraft:unbreaking"), NSID("minecraft:sharpness")),
        "unbreaking compatible with sharpness");

    // ---- Verify applicable_to resolution --------------------------------
    {
        const auto& si = ench_reg.at(NSID("minecraft:sharpness"));
        expect(si.supported_items.size() == 1,
               "sharpness applicable to 1 category");
        expect(si.supported_items.contains(NSID("#minecraft:sword")),
               "sharpness applicable to sword tag");
    }
    {
        const auto& pi = ench_reg.at(NSID("minecraft:protection"));
        expect(pi.supported_items.size() == 1,
               "protection applicable to 1 category");
        expect(pi.supported_items.contains(NSID("#minecraft:helmet")),
               "protection applicable to helmet tag");
    }
    {
        const auto& ui = ench_reg.at(NSID("minecraft:unbreaking"));
        expect(ui.supported_items.size() == 2,
               "unbreaking applicable to 2 categories");
        expect(ui.supported_items.contains(NSID("#minecraft:sword")),
               "unbreaking applicable to sword tag");
        expect(ui.supported_items.contains(NSID("#minecraft:helmet")),
               "unbreaking applicable to helmet tag");
    }

    // ---- Verify exclusive_with namespace resolution ---------------------
    // Bare name "smite" should be resolved to namespaced "minecraft:smite"
    {
        const auto& excl = ench_reg.get_exclusive_set(NSID("minecraft:sharpness"));
        expect(excl.size() == 1, "sharpness exclusive_set has 1 entry");
        expect(excl.contains(NSID("minecraft:smite")),
               "bare 'smite' resolved to namespaced NSID minecraft:smite");
    }

    std::cout << "PASS: test_loader_resolve_full" << std::endl;
}

// ---------------------------------------------------------------------------
// 6. supported_items cross-validation: the enchant keeps a raw `#tag`
//    reference when the tag is defined in the base tag registry, and drops
//    references that cannot resolve.
// ---------------------------------------------------------------------------
void test_loader_supported_items_resolution() {
    // Mod enchant references vanilla item tag `#minecraft:swords` — base_tags
    // provides the definition.
    std::vector<business::loader::EquipmentData> no_eq;
    std::vector<business::loader::EnchantmentData> ench_data;
    ench_data.push_back({
        "minecraft:leeching", "Leeching", 1, 2, 2,
        {"sharpness"}, {"#minecraft:swords"}
    });
    RegistryLoader loader;

    TagRegistry base_tags;
    base_tags.insert({NSID("#minecraft:swords"), "swords"});

    TagRegistry tag_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    loader.resolve(ench_data, no_eq, tag_reg, eq_reg, ench_reg, &base_tags);
    const auto& e = ench_reg.at(NSID("minecraft:leeching"));
    expect(e.supported_items.contains(NSID("#minecraft:swords")),
           "supported_items keeps #tag reference");
    std::cout << "PASS: test_loader_supported_items_resolution" << std::endl;
}

// ---------------------------------------------------------------------------
// 6b. Concrete item-ID references resolve against the profile's own equipment
//     registry; references that cannot resolve are dropped entirely.
// ---------------------------------------------------------------------------
void test_loader_supported_items_concrete_and_drop() {
    std::vector<business::loader::EquipmentData> eq_data;
    eq_data.push_back({"minecraft:diamond_sword", "Diamond Sword", "sword", 1561});

    std::vector<business::loader::EnchantmentData> ench_data;
    // Concrete ID that exists in eq_reg → kept with the reference preserved.
    ench_data.push_back({
        "minecraft:test_concrete", "Test Concrete", 1, 1, 1,
        {}, {"minecraft:diamond_sword"}
    });
    // Concrete ID that does NOT exist → no resolvable supported_items → dropped.
    ench_data.push_back({
        "minecraft:test_bad_ref", "Test Bad Ref", 1, 1, 1,
        {}, {"minecraft:not_a_real_item"}
    });
    // #tag that is not defined → dropped.
    ench_data.push_back({
        "minecraft:test_bad_tag", "Test Bad Tag", 1, 1, 1,
        {}, {"#minecraft:undefined_tag"}
    });

    RegistryLoader loader;
    TagRegistry tag_reg;
    EquipmentRegistry eq_reg;
    EnchantmentRegistry ench_reg;
    loader.resolve(ench_data, eq_data, tag_reg, eq_reg, ench_reg);

    expect(ench_reg.size() == 1, "only the resolvable enchantment survives");
    expect(ench_reg.contains(NSID("minecraft:test_concrete")),
           "concrete ID enchantment kept");
    expect(ench_reg.at(NSID("minecraft:test_concrete")).supported_items.contains(
               NSID("minecraft:diamond_sword")),
           "concrete ID reference preserved in supported_items");
    expect(!ench_reg.contains(NSID("minecraft:test_bad_ref")),
           "unresolvable concrete ID enchantment dropped");
    expect(!ench_reg.contains(NSID("minecraft:test_bad_tag")),
           "undefined #tag enchantment dropped");

    std::cout << "PASS: test_loader_supported_items_concrete_and_drop" << std::endl;
}

// ---------------------------------------------------------------------------
// 7. Parser-level vanilla tag fallback: a mod profile referencing a VANILLA
//    tag (`#minecraft:...`) resolves even though the profile defines no tags.
// ---------------------------------------------------------------------------
void test_loader_vanilla_tag_fallback() {
    // Native-JSON profile whose enchant references the vanilla curse tag and
    // the vanilla "sword" category, with no equipments/tags of its own.
    const std::string content = R"({
        "name": "tagfallback",
        "enchantments": [
            { "id": "mod_cursed", "name": "Mod Cursed", "platform": "java",
              "max_level": 1, "limited_level": 1, "multiplier": 8,
              "exclusive_set": ["#minecraft:enchantment/curse"],
              "supported_items": ["#minecraft:swords"], "is_treasure": true }
        ],
        "equipments": [],
        "categories": [],
        "tags": {}
    })";

    static int counter = 0;
    auto path = std::filesystem::temp_directory_path() /
                ("besq_tag_test_" + std::to_string(++counter) + ".json");
    {
        std::ofstream f(path);
        f << content;
    }

    ProfileLoader loader;
    Profile p = loader.load(path);
    const auto& e = p.ench().at(NSID("minecraft:mod_cursed"));

    // The vanilla curse tag expands to binding_curse + vanishing_curse.
    expect(e.exclusive_set.contains(NSID("minecraft:binding_curse")),
           "vanilla curse tag expanded to binding_curse");
    expect(e.exclusive_set.contains(NSID("minecraft:vanishing_curse")),
           "vanilla curse tag expanded to vanishing_curse");
    // The raw #minecraft:swords (real MC item tag) reference resolves via the
    // vanilla fallback (builtin tag registry).
    expect(e.supported_items.contains(NSID("#minecraft:swords")),
           "vanilla swords tag resolves");

    std::cout << "PASS: test_loader_vanilla_tag_fallback" << std::endl;
}

// ---------------------------------------------------------------------------
// 7b. resolve_with_base: profile DTOs merge on top of an already-populated
//     universe (vanilla tags/equipment). A concrete vanilla item reference
//     resolves even though the profile defines no equipment of its own.
// ---------------------------------------------------------------------------
void test_loader_resolve_with_base() {
    // Universe (vanilla): elytra equipment + its category tag.
    std::vector<business::loader::EquipmentData> vanilla_eq;
    vanilla_eq.push_back({"minecraft:elytra", "Elytra", "elytra", 432});

    // Profile: a mod enchant referencing the concrete vanilla item.
    std::vector<business::loader::EnchantmentData> profile_ench;
    profile_ench.push_back({"mod:glide", "Glide", 2, 3, 3, {}, {"minecraft:elytra"}});

    RegistryLoader loader;
    TagRegistry tag_reg;
    tag_reg.insert({NSID("#minecraft:elytra"), "elytra"});
    EquipmentRegistry eq_reg;       // universe first (vanilla)
    EnchantmentRegistry ench_reg;

    loader.from_dto(eq_reg, tag_reg, vanilla_eq);
    loader.resolve_with_base(profile_ench, {}, tag_reg, eq_reg, ench_reg);

    expect(ench_reg.contains(NSID("mod:glide")),
           "concrete vanilla item ref resolves");
    expect(ench_reg.at(NSID("mod:glide")).supported_items.contains(NSID("minecraft:elytra")),
           "supported_items keeps the concrete item NSID");
    TEST_PASS("test_loader_resolve_with_base");
}

// ---------------------------------------------------------------------------
// 7c. Concrete vanilla item resolves cross-profile through the ProfileLoader:
//     a mod profile referencing `minecraft:elytra` loads successfully even
//     though the profile defines no equipment; the vanilla universe provides
//     the validation fallback.
// ---------------------------------------------------------------------------
void test_loader_concrete_item_vanilla_universe() {
    const std::string content = R"({
        "name": "glide_mod",
        "enchantments": [
            { "id": "mod:glide", "name": "Glide", "platform": "java",
              "max_level": 2, "limited_level": 3, "multiplier": 3,
              "exclusive_set": [],
              "supported_items": ["minecraft:elytra"], "is_treasure": true }
        ],
        "equipments": [],
        "categories": [],
        "tags": {}
    })";

    static int counter = 0;
    auto path = std::filesystem::temp_directory_path() /
                ("besq_glide_test_" + std::to_string(++counter) + ".json");
    {
        std::ofstream f(path);
        f << content;
    }

    ProfileLoader loader;
    Profile p = loader.load(path);
    expect(p.ench().contains(NSID("mod:glide")),
           "concrete vanilla item ref resolves via vanilla universe");
    expect(p.ench().at(NSID("mod:glide")).supported_items.contains(NSID("minecraft:elytra")),
           "supported_items keeps the concrete item NSID");
    // The profile-only filter must keep vanilla content OUT of the profile:
    // the vanilla universe is only the validation fallback, not content.
    expect(p.eq().size() == 0, "profile has no equipment of its own");
    expect(!p.ench().contains(NSID("minecraft:sharpness")),
           "vanilla enchantment excluded from profile");
    TEST_PASS("test_loader_concrete_item_vanilla_universe");
}

// =============================================================================
// Section B -- TagResolver
// =============================================================================

// ---------------------------------------------------------------------------
// 5. Resolve a single tag with concrete items.
// ---------------------------------------------------------------------------
void test_tag_resolve_basic() {
    TagResolver resolver;

    // Register a tag: "minecraft:swords" -> {diamond_sword, iron_sword}
    resolver.add_tag("minecraft:swords",
        {"minecraft:diamond_sword", "minecraft:iron_sword"});

    auto result = resolver.resolve("#minecraft:swords");
    expect(result.size() == 2, "swords tag should have 2 entries");
    expect(result.contains("minecraft:diamond_sword"),
           "swords tag contains diamond_sword");
    expect(result.contains("minecraft:iron_sword"),
           "swords tag contains iron_sword");

    // Concrete ID passthrough (no '#') should return a set containing itself
    auto direct = resolver.resolve("minecraft:sharpness");
    expect(direct.size() == 1, "concrete id returns set of 1");
    expect(direct.contains("minecraft:sharpness"), "passthrough works");

    std::cout << "PASS: test_tag_resolve_basic" << std::endl;
}

// ---------------------------------------------------------------------------
// 6. Resolve chained (composite) tags with transitive references.
// ---------------------------------------------------------------------------
void test_tag_resolve_composite() {
    TagResolver resolver;

    // weapons -> sharpness + #melee (tag reference)
    // melee   -> smite + bane_of_arthropods
    resolver.add_tag("minecraft:weapons",
        {"minecraft:sharpness", "#minecraft:melee"});
    resolver.add_tag("minecraft:melee",
        {"minecraft:smite", "minecraft:bane_of_arthropods"});

    auto result = resolver.resolve("#minecraft:weapons");
    expect(result.size() == 3,
           "weapons should resolve chained refs to 3 entries");
    expect(result.contains("minecraft:sharpness"),
           "direct entry via weapons tag");
    expect(result.contains("minecraft:smite"),
           "transitive via melee tag");
    expect(result.contains("minecraft:bane_of_arthropods"),
           "transitive via melee tag");
    std::cout << "PASS: test_tag_resolve_composite" << std::endl;
}

// ---------------------------------------------------------------------------
// 7. Resolve a tag that does not exist -- should return empty set.
// ---------------------------------------------------------------------------
void test_tag_unknown_tag() {
    TagResolver resolver;

    // No tags registered at all
    auto result = resolver.resolve("#minecraft:nonexistent");
    expect(result.empty(),
           "unknown tag with empty resolver returns empty set");

    // Some tags exist, but the queried tag does not
    resolver.add_tag("minecraft:known", {"minecraft:something"});
    auto miss = resolver.resolve("#minecraft:other");
    expect(miss.empty(),
           "non-existent tag still returns empty even with other tags present");

    // Empty reference should also return empty
    auto empty_ref = resolver.resolve("");
    expect(empty_ref.empty(), "empty string reference returns empty set");

    std::cout << "PASS: test_tag_unknown_tag" << std::endl;
}

// ---------------------------------------------------------------------------
// 8. Resolve a simple tag with concrete items (standalone, no registry init).
// ---------------------------------------------------------------------------
void test_tag_resolver_basic() {
    TagResolver resolver;
    resolver.add_tag("minecraft:swords",
        {"minecraft:diamond_sword", "minecraft:iron_sword"});

    auto result = resolver.resolve("#minecraft:swords");
    expect(result.size() == 2, "swords tag should have 2 entries");
    expect(result.contains("minecraft:diamond_sword"),
           "swords tag contains diamond_sword");
    expect(result.contains("minecraft:iron_sword"),
           "swords tag contains iron_sword");
    TEST_PASS("test_tag_resolver_basic");
}

// ---------------------------------------------------------------------------
// 9. Resolve a composite tag with transitive (nested) tag references.
// ---------------------------------------------------------------------------
void test_tag_resolver_nested() {
    TagResolver resolver;

    // swords -> #minecraft:all_swords (tag reference)
    // all_swords -> diamond_sword
    resolver.add_tag("minecraft:swords",
        {"#minecraft:all_swords"});
    resolver.add_tag("minecraft:all_swords",
        {"minecraft:diamond_sword"});

    auto result = resolver.resolve("#minecraft:swords");
    expect(result.size() == 1,
           "nested swords tag resolves to 1 entry");
    expect(result.contains("minecraft:diamond_sword"),
           "transitive resolution yields diamond_sword");
    TEST_PASS("test_tag_resolver_nested");
}

// ---------------------------------------------------------------------------
// 10. Resolve a tag that does not exist in the resolver.
// ---------------------------------------------------------------------------
void test_tag_resolver_unknown() {
    TagResolver resolver;

    auto result = resolver.resolve("#minecraft:nonexistent");
    expect(result.empty(),
           "non-existent tag returns empty set");
    TEST_PASS("test_tag_resolver_unknown");
}

// ---------------------------------------------------------------------------
// 11. Resolve a bare ID (no '#') returns a set containing the ID itself.
// ---------------------------------------------------------------------------
void test_tag_resolver_no_hash() {
    TagResolver resolver;

    auto result = resolver.resolve("minecraft:sharpness");
    expect(result.size() == 1,
           "bare ID reference returns set of 1");
    expect(result.contains("minecraft:sharpness"),
           "bare ID passthrough works");
    TEST_PASS("test_tag_resolver_no_hash");
}

// ---------------------------------------------------------------------------
// 12. Reverse lookup: given a concrete item ID, return the set of tags that
//     contain it, as `#`-prefixed NSIDs (matches supported_items form).
// ---------------------------------------------------------------------------
void test_tag_tags_of() {
    TagResolver resolver;
    resolver.add_tag("minecraft:swords",
        {"minecraft:diamond_sword", "minecraft:iron_sword"});
    resolver.add_tag("minecraft:enchantable/durability",
        {"minecraft:diamond_sword"});

    auto tags = resolver.tags_of("minecraft:diamond_sword");
    expect(tags.size() == 2, "diamond_sword belongs to 2 tags");
    expect(tags.contains(NSID("#minecraft:swords")),
           "tags_of returns #-prefixed swords tag");
    expect(tags.contains(NSID("#minecraft:enchantable/durability")),
           "tags_of returns durability tag");

    auto none = resolver.tags_of("minecraft:nonexistent");
    expect(none.empty(), "unknown item has no tags");
    TEST_PASS("test_tag_tags_of");
}

// =============================================================================
// Section C -- Serializer tests
// =============================================================================

// ---------------------------------------------------------------------------
// 12. Serialize a Profile to JSON and verify expected fields.
// ---------------------------------------------------------------------------
void test_serialize_profile() {
    Profile profile("test:profile");
    profile.set_description("Test description");
    profile.set_version("1.0.0");

    Json json;
    json << profile;

    expect(json.is_valid(),           "profile JSON is valid");
    expect(json.has("name"),          "JSON has name field");
    expect(json.has("description"),   "JSON has description field");
    expect(json.has("version"),       "JSON has version field");
    expect(json.has("enchantments"),  "JSON has enchantments field");
    expect(json.has("equipments"),    "JSON has equipments field");
    expect(json.has("tags"),          "JSON has tags field");

    expect(json["name"].as_string() == "test:profile",
           "name field matches");
    expect(json["description"].as_string() == "Test description",
           "description field matches");
    expect(json["version"].as_string() == "1.0.0",
           "version field matches");

    TEST_PASS("test_serialize_profile");
}

// ---------------------------------------------------------------------------
// 13. Profile serialization roundtrip: serialize, deserialize, verify.
// ---------------------------------------------------------------------------
void test_serialize_profile_roundtrip() {
    Profile original("test:roundtrip");
    original.set_description("Roundtrip test");

    EnchInfo sharpness(
        NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false,
        std::unordered_set<NSID>{}, std::unordered_set<NSID>{}
    );
    original.add_enchantment(sharpness);

    // -- Serialize --
    Json json;
    json << original;

    // -- Deserialize --
    Profile restored;
    json >> restored;

    // -- Verify --
    expect(restored.name() == "test:roundtrip",
           "name preserved after roundtrip");
    expect(restored.ench().contains(NSID("minecraft:sharpness")),
           "enchantment present after roundtrip");
    expect(restored.ench().at(NSID("minecraft:sharpness")).max_level == 5,
           "sharpness max_level preserved after roundtrip");

    TEST_PASS("test_serialize_profile_roundtrip");
}

// ---------------------------------------------------------------------------
// 14. Load a tag from a Json DOM object and verify resolution.
// ---------------------------------------------------------------------------
void test_tag_load_tag_json() {
    TagResolver resolver;

    // Build Json with values array
    Json::Object root_obj;
    {
        Json::Array values;
        values.push_back(Json("minecraft:diamond_sword"));
        values.push_back(Json("minecraft:iron_sword"));
        root_obj["values"] = Json(values);
    }
    Json tag_json(root_obj);

    // Load via load_tag_json
    resolver.load_tag_json("minecraft:tags/swords", tag_json);

    // Resolve and verify
    auto result = resolver.resolve("#minecraft:tags/swords");
    expect(result.size() == 2,
           "load_tag_json resolution should have 2 items");
    expect(result.contains("minecraft:diamond_sword"),
           "load_tag_json result contains diamond_sword");
    expect(result.contains("minecraft:iron_sword"),
           "load_tag_json result contains iron_sword");

    TEST_PASS("test_tag_load_tag_json");
}

// ---------------------------------------------------------------------------
// 15. Load a tag from a raw JSON string and verify resolution.
// ---------------------------------------------------------------------------
void test_tag_load_tag_content() {
    TagResolver resolver;

    // Load via load_tag_content with raw JSON string
    resolver.load_tag_content("minecraft:swords",
        R"({"values": ["minecraft:diamond_sword", "minecraft:iron_sword"]})");

    // Resolve and verify
    auto result = resolver.resolve("#minecraft:swords");
    expect(result.size() == 2,
           "load_tag_content resolution should have 2 items");
    expect(result.contains("minecraft:diamond_sword"),
           "load_tag_content result contains diamond_sword");
    expect(result.contains("minecraft:iron_sword"),
           "load_tag_content result contains iron_sword");

    TEST_PASS("test_tag_load_tag_content");
}

} // anonymous namespace

// =============================================================================
// main
// =============================================================================
int main() {
    try {
        // Section A -- RegistryLoader
        test_loader_ench_dto_to_reg();
        test_loader_eq_dto_to_reg();
        test_loader_json_roundtrip();
        test_loader_resolve_full();
        test_loader_supported_items_resolution();
        test_loader_supported_items_concrete_and_drop();
        test_loader_vanilla_tag_fallback();
        test_loader_resolve_with_base();
        test_loader_concrete_item_vanilla_universe();

        // Section B -- TagResolver
        test_tag_resolve_basic();
        test_tag_resolve_composite();
        test_tag_unknown_tag();

        // Section B (continued) -- TagResolver standalone
        test_tag_resolver_basic();
        test_tag_resolver_nested();
        test_tag_resolver_unknown();
        test_tag_resolver_no_hash();
        test_tag_tags_of();

        // Section C -- Serializer
        test_serialize_profile();
        test_serialize_profile_roundtrip();

        // Section D -- Tag loading
        test_tag_load_tag_json();
        test_tag_load_tag_content();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
