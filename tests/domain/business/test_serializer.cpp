#include "framework/test_utils.h"
#include "domain/business/components/Serializer.h"
#include "domain/orchestration/components/EnchSerializer.h"

// ─── test_serialize_ench ───────────────────────────────────────────────
// Round-trip an Ench through Json: serialize, deserialize, verify fields.

void test_serialize_ench() {
    Ench ench(NSID("minecraft:sharpness"), "Sharpness", 5);

    Json j;
    j << ench;

    Ench e2;
    j >> e2;

    expect(e2 == ench, "serialize_ench: operator== match");
    expect(e2.id == NSID("minecraft:sharpness"), "serialize_ench: id");
    // name field is deprecated — after round-trip it becomes empty
    expect(e2.level == 5, "serialize_ench: level");

    std::cout << "PASS: test_serialize_ench" << std::endl;
}

// ─── test_serialize_enchinfo ───────────────────────────────────────────
// Round-trip EnchInfo with exclusive_set and applicable_equipments.

void test_serialize_enchinfo() {
    std::unordered_set<NSID> exclusive_set = {
        NSID("minecraft:smite"),
        NSID("minecraft:bane_of_arthropods")
    };
    std::unordered_set<NSID> applicable = {
        NSID("#minecraft:sword"),
        NSID("#minecraft:axe")
    };
    EnchInfo info{
        NSID("minecraft:sharpness"), "Sharpness", MCE::All,
        5, 5, 1, false,
        exclusive_set, applicable
    };

    Json j;
    j << info;

    EnchInfo i2;
    j >> i2;

    expect(i2.id == NSID("minecraft:sharpness"), "enchinfo round-trip: id");
    expect(i2.name == "Sharpness", "enchinfo round-trip: name");
    expect(i2.supported_platform == MCE::All, "enchinfo round-trip: platform");
    expect(i2.max_level == 5, "enchinfo round-trip: max_level");
    expect(i2.limited_level == 5, "enchinfo round-trip: limited_level");
    expect(i2.multiplier == 1, "enchinfo round-trip: multiplier");
    expect(i2.is_treasure == false, "enchinfo round-trip: is_treasure");
    expect(i2.exclusive_set.size() == 2, "enchinfo round-trip: exclusive_set size");
    expect(i2.exclusive_set.count(NSID("minecraft:smite")) == 1,
           "enchinfo round-trip: exclusive_set contains smite");
    expect(i2.exclusive_set.count(NSID("minecraft:bane_of_arthropods")) == 1,
           "enchinfo round-trip: exclusive_set contains bane");
    expect(i2.applicable_equipments.size() == 2,
           "enchinfo round-trip: applicable_equipments size");
    expect(i2.applicable_equipments.count(NSID("#minecraft:sword")) == 1,
           "enchinfo round-trip: applicable contains sword");
    expect(i2.applicable_equipments.count(NSID("#minecraft:axe")) == 1,
           "enchinfo round-trip: applicable contains axe");

    std::cout << "PASS: test_serialize_enchinfo" << std::endl;
}

// ─── test_serialize_enchset ────────────────────────────────────────────
// Round-trip an EnchSet with 3 entries.

void test_serialize_enchset() {
    EnchSet set;
    set.emplace(NSID("minecraft:sharpness"), "Sharpness", 5);
    set.emplace(NSID("minecraft:unbreaking"), "Unbreaking", 3);
    set.emplace(NSID("minecraft:mending"), "Mending", 1);

    Json j;
    j << set;

    EnchSet s2;
    j >> s2;

    expect(s2.size() == 3, "enchset round-trip: size 3");
    expect(s2.find(NSID("minecraft:sharpness")) != s2.end(),
           "enchset round-trip: contains sharpness");
    expect(s2.find(NSID("minecraft:unbreaking")) != s2.end(),
           "enchset round-trip: contains unbreaking");
    expect(s2.find(NSID("minecraft:mending")) != s2.end(),
           "enchset round-trip: contains mending");

    std::cout << "PASS: test_serialize_enchset" << std::endl;
}

// ─── test_serialize_equipment ──────────────────────────────────────────
// Round-trip an Equipment instance.

void test_serialize_equipment() {
    Equipment eq{
        NSID("minecraft:diamond_sword"), "Diamond Sword",
        EquipmentTag::sword(), 1561
    };

    Json j;
    j << eq;

    Equipment e2;
    j >> e2;

    expect(e2.id == NSID("minecraft:diamond_sword"),
           "equipment round-trip: id");
    expect(e2.name == "Diamond Sword",
           "equipment round-trip: name");
    expect(e2.category == EquipmentTag::sword(),
           "equipment round-trip: category");
    expect(e2.max_durability == 1561,
           "equipment round-trip: max_durability");

    std::cout << "PASS: test_serialize_equipment" << std::endl;
}

// ─── test_serialize_equipment_tag ──────────────────────────────────────
// Round-trip an EquipmentTag.

void test_serialize_equipment_tag() {
    EquipmentTag tag{NSID("#minecraft:sword"), "sword"};

    Json j;
    j << tag;

    EquipmentTag t2;
    j >> t2;

    expect(t2.id == NSID("#minecraft:sword"),
           "equipment_tag round-trip: id");
    expect(t2.name == "sword",
           "equipment_tag round-trip: name");

    std::cout << "PASS: test_serialize_equipment_tag" << std::endl;
}

// ─── test_serialize_item ───────────────────────────────────────────────
// Round-trip an Item with enchantments.

void test_serialize_item() {
    EnchSet enchs;
    enchs.emplace(NSID("minecraft:sharpness"), "Sharpness", 5);
    enchs.emplace(NSID("minecraft:unbreaking"), "Unbreaking", 3);
    Item item(NSID("minecraft:diamond_sword"), enchs, 2, 500);

    Json j;
    j << item;

    Item i2;
    j >> i2;

    expect(i2.id == NSID("minecraft:diamond_sword"),
           "item round-trip: id");
    expect(i2.enchantments.size() == 2,
           "item round-trip: enchantments size");
    expect(i2.enchantments.find(NSID("minecraft:sharpness")) != i2.enchantments.end(),
           "item round-trip: contains sharpness");
    expect(i2.enchantments.find(NSID("minecraft:unbreaking")) != i2.enchantments.end(),
           "item round-trip: contains unbreaking");
    expect(i2.prior_penalty == 2,
           "item round-trip: prior_penalty");
    expect(i2.durability == 500,
           "item round-trip: durability");

    std::cout << "PASS: test_serialize_item" << std::endl;
}

// ─── test_serialize_solution_step ──────────────────────────────────────
// Round-trip a Solution::EnchStep with costs.

void test_serialize_solution_step() {
    EnchSet step_enchs;
    step_enchs.emplace(NSID("minecraft:sharpness"), "Sharpness", 3);
    Item item_a(NSID("minecraft:diamond_sword"), step_enchs, 1, 500);

    EnchSet step_enchs_b;
    step_enchs_b.emplace(NSID("minecraft:sharpness"), "Sharpness", 5);
    Item item_b(NSID("minecraft:enchanted_book"), step_enchs_b, 0);

    Solution::EnchStep step{item_a, item_b, 10, 50};

    Json j;
    j << step;

    Solution::EnchStep s2;
    j >> s2;

    expect(s2.item_a.id == NSID("minecraft:diamond_sword"),
           "step round-trip: item_a id");
    expect(s2.item_b.id == NSID("minecraft:enchanted_book"),
           "step round-trip: item_b id");
    expect(s2.exp_level_cost == 10,
           "step round-trip: exp_level_cost");
    expect(s2.exp_cost == 50,
           "step round-trip: exp_cost");

    std::cout << "PASS: test_serialize_solution_step" << std::endl;
}

// ─── test_serialize_solution ───────────────────────────────────────────
// Round-trip a full Solution with steps and metadata.

void test_serialize_solution() {
    // Original enchantments applied
    EnchSet original;
    original.emplace(NSID("minecraft:sharpness"), "Sharpness", 3);

    // Target item
    EnchSet target_enchs;
    target_enchs.emplace(NSID("minecraft:sharpness"), "Sharpness", 5);
    target_enchs.emplace(NSID("minecraft:unbreaking"), "Unbreaking", 3);
    Item target(NSID("minecraft:diamond_sword"), target_enchs, 2, 500);

    // Available items (sacrifice items)
    Item book(NSID("minecraft:enchanted_book"), EnchSet{}, 0);
    std::vector<Item> available = {book};

    // Forge steps
    EnchSet step_enchs_a;
    step_enchs_a.emplace(NSID("minecraft:sharpness"), "Sharpness", 5);
    Solution::EnchStep step{
        Item(NSID("minecraft:diamond_sword"), EnchSet{}, 0, 500),
        Item(NSID("minecraft:enchanted_book"), step_enchs_a, 0),
        15, 60
    };
    std::vector<Solution::EnchStep> steps = {step};

    // Metadata
    SolutionMetaData meta;
    meta.algorithm_name    = "astar";
    meta.algorithm_version = "1.0";
    meta.created_at        = std::chrono::system_clock::now();
    meta.computation_time  = std::chrono::milliseconds(500);
    meta.mode              = AlgorithmMode::direct;
    meta.task_id           = 42;

    auto sol = Solution::make(
        MCE::Java, original, target, available, steps, true, meta
    );

    Json j;
    j << sol;

    Solution s2;
    j >> s2;

    // Core fields
    expect(s2.total_exp_level_cost == sol.total_exp_level_cost,
           "solution round-trip: total_exp_level_cost");
    expect(s2.total_exp_cost == sol.total_exp_cost,
           "solution round-trip: total_exp_cost");
    expect(s2.is_success == true,
           "solution round-trip: is_success");
    expect(s2.platform == MCE::Java,
           "solution round-trip: platform");

    // Metadata
    expect(s2.metadata.algorithm_name == "astar",
           "solution round-trip: algorithm_name");
    expect(s2.metadata.algorithm_version == "1.0",
           "solution round-trip: algorithm_version");
    expect(s2.metadata.mode == AlgorithmMode::direct,
           "solution round-trip: mode");
    expect(s2.metadata.task_id == 42,
           "solution round-trip: task_id");

    // Collections
    expect(s2.steps.size() == 1,
           "solution round-trip: steps count");
    expect(s2.steps[0].exp_level_cost == 15,
           "solution round-trip: step[0].exp_level_cost");
    expect(s2.steps[0].exp_cost == 60,
           "solution round-trip: step[0].exp_cost");
    expect(s2.available_items.size() == 1,
           "solution round-trip: available_items count");
    expect(s2.available_items[0].id == NSID("minecraft:enchanted_book"),
           "solution round-trip: available item id");

    std::cout << "PASS: test_serialize_solution" << std::endl;
}

// ─── test_serialize_equipment_registry ─────────────────────────────────
// Round-trip EquipmentRegistry with 2 entries.

void test_serialize_equipment_registry() {
    std::vector<Equipment> eqs = {
        Equipment{
            NSID("minecraft:diamond_sword"), "Diamond Sword",
            EquipmentTag::sword(), 1561
        },
        Equipment{
            NSID("minecraft:diamond_chestplate"), "Diamond Chestplate",
            EquipmentTag::chestplate(), 528
        }
    };
    EquipmentRegistry reg(eqs);

    Json j;
    j << reg;

    EquipmentRegistry reg2;
    j >> reg2;

    expect(reg2.size() == 2,
           "equipment registry round-trip: size 2");
    expect(reg2.contains(NSID("minecraft:diamond_sword")),
           "equipment registry round-trip: contains diamond_sword");
    expect(reg2.contains(NSID("minecraft:diamond_chestplate")),
           "equipment registry round-trip: contains diamond_chestplate");

    std::cout << "PASS: test_serialize_equipment_registry" << std::endl;
}

// ─── test_serialize_equipment_tag_registry ─────────────────────────────
// Round-trip TagRegistry with 2 tags.

void test_serialize_equipment_tag_registry() {
    std::vector<EquipmentTag> tags = {
        {NSID("#minecraft:sword"), "sword"},
        {NSID("#minecraft:axe"), "axe"}
    };
    TagRegistry reg(tags);

    Json j;
    j << reg;

    TagRegistry reg2;
    j >> reg2;

    expect(reg2.size() == 2,
           "equipment tag registry round-trip: size 2");
    // TagRegistry deserialization reconstructs tags from name:
    //   id = "#minecraft:<name>"
    expect(reg2.contains(NSID("#minecraft:sword")),
           "equipment tag registry round-trip: contains sword");
    expect(reg2.contains(NSID("#minecraft:axe")),
           "equipment tag registry round-trip: contains axe");

    std::cout << "PASS: test_serialize_equipment_tag_registry" << std::endl;
}

// ─── test_serialize_ench_registry ──────────────────────────────────────
// Round-trip EnchantmentRegistry with 2 entries including exclusive_set.

void test_serialize_ench_registry() {
    std::unordered_set<NSID> excl_sharp = {NSID("minecraft:smite")};
    std::unordered_set<NSID> excl_smite = {NSID("minecraft:sharpness")};
    std::unordered_set<NSID> applicable = {NSID("#minecraft:sword")};

    std::vector<EnchInfo> infos = {
        {NSID("minecraft:sharpness"), "Sharpness", MCE::All,
         5, 5, 1, false, excl_sharp, applicable},
        {NSID("minecraft:smite"), "Smite", MCE::All,
         5, 5, 1, false, excl_smite, applicable}
    };
    EnchantmentRegistry reg(infos);

    Json j;
    j << reg;

    EnchantmentRegistry reg2;
    j >> reg2;

    expect(reg2.size() == 2,
           "ench registry round-trip: size 2");
    expect(reg2.contains(NSID("minecraft:sharpness")),
           "ench registry round-trip: contains sharpness");
    expect(reg2.contains(NSID("minecraft:smite")),
           "ench registry round-trip: contains smite");
    expect(reg2.is_incompatible(NSID("minecraft:sharpness"), NSID("minecraft:smite")),
           "ench registry round-trip: sharpness incompatible with smite");
    expect(reg2.is_incompatible(NSID("minecraft:smite"), NSID("minecraft:sharpness")),
           "ench registry round-trip: smite incompatible with sharpness");

    std::cout << "PASS: test_serialize_ench_registry" << std::endl;
}

// ─── test_serializer_mce_helpers ───────────────────────────────────────
// Test mce_to_string and string_to_mce round-trip for all MCE values.

void test_serializer_mce_helpers() {
    struct TestCase { MCE value; std::string_view expected; };
    TestCase cases[] = {
        {MCE::None,    "none"},
        {MCE::Java,    "java"},
        {MCE::Bedrock, "bedrock"},
        {MCE::All,     "all"},
    };

    for (const auto& tc : cases) {
        auto str = Serializer::mce_to_string(tc.value);
        expect(str == tc.expected,
               "mce_to_string(" + std::to_string(static_cast<int>(tc.value))
               + "): expected " + std::string(tc.expected));
        auto mce = Serializer::string_to_mce(str);
        expect(mce == tc.value,
               "string_to_mce round-trip for " + std::string(tc.expected));
    }

    // Case insensitivity of string_to_mce
    expect(Serializer::string_to_mce("JAVA") == MCE::Java,
           "string_to_mce uppercase JAVA");
    expect(Serializer::string_to_mce("BedRock") == MCE::Bedrock,
           "string_to_mce mixed case BedRock");
    expect(Serializer::string_to_mce("ALL") == MCE::All,
           "string_to_mce uppercase ALL");
    expect(Serializer::string_to_mce("unknown") == MCE::None,
           "string_to_mce unknown returns None");

    std::cout << "PASS: test_serializer_mce_helpers" << std::endl;
}

// ─── test_serializer_to_from_string ────────────────────────────────────
// Round-trip Json through Serializer::to_string and ::from_string.

void test_serializer_to_from_string() {
    Json::Object obj;
    obj["name"]  = Json(Json::String("test"));
    obj["value"] = Json(Json::Number(42));
    Json original(obj);

    std::string str = Serializer::to_string(original);
    expect(!str.empty(), "to_string produces non-empty string");

    Json parsed = Serializer::from_string(str);
    expect(parsed.is_valid(), "from_string returns valid Json");

    // Round-trip through compact style
    std::string compact = Serializer::to_string(original, Json::Compact);
    Json parsed_compact = Serializer::from_string(compact);
    expect(parsed_compact.is_valid(),
           "from_string on compact style returns valid Json");
    expect(parsed == original,
           "Json after to_string/from_string round-trip equals original");

    std::cout << "PASS: test_serializer_to_from_string" << std::endl;
}

// ─── test_to_mc_official_strings ──────────────────────────────────────────
// Serialize EnchInfo vector to MC official format strings and verify keys/content.

void test_to_mc_official_strings() {
    // Create TagRegistry with sword tag
    TagRegistry tag_reg;
    tag_reg.insert({NSID("#minecraft:sword"), "sword"});

    // Create EnchInfo vector with 2 enchants applicable to sword
    std::unordered_set<NSID> excl_sharp = {NSID("minecraft:smite")};
    std::unordered_set<NSID> excl_smite = {NSID("minecraft:sharpness")};
    std::unordered_set<NSID> sword_applicable = {NSID("#minecraft:sword")};

    std::vector<EnchInfo> infos;
    infos.emplace_back(NSID("minecraft:sharpness"), "Sharpness", MCE::All,
                       5, 5, 1, false, excl_sharp, sword_applicable);
    infos.emplace_back(NSID("minecraft:smite"), "Smite", MCE::All,
                       5, 5, 1, false, excl_smite, sword_applicable);

    // Serialize to MC official format
    auto result = EnchSerializer::to_mc_official_strings(infos, tag_reg);

    // Verify map size
    expect(result.size() == 2,
           "mc_official_strings should have 2 entries");

    // Verify keys exist
    expect(result.find("data/minecraft/enchantment/sharpness.json") != result.end(),
           "mc_official_strings contains sharpness.json key");
    expect(result.find("data/minecraft/enchantment/smite.json") != result.end(),
           "mc_official_strings contains smite.json key");

    // Verify content strings are non-empty
    const auto& sharp_content = result.at("data/minecraft/enchantment/sharpness.json");
    expect(!sharp_content.empty(),
           "sharpness content is non-empty");
    const auto& smite_content = result.at("data/minecraft/enchantment/smite.json");
    expect(!smite_content.empty(),
           "smite content is non-empty");

    // Verify content contains expected fields
    expect(sharp_content.find("anvil_cost") != std::string::npos,
           "sharpness content contains anvil_cost");
    expect(sharp_content.find("max_level") != std::string::npos,
           "sharpness content contains max_level");
    expect(sharp_content.find("exclusive_set") != std::string::npos,
           "sharpness content contains exclusive_set");
    expect(sharp_content.find("supported_items") != std::string::npos,
           "sharpness content contains supported_items");
    expect(sharp_content.find("minecraft:smite") != std::string::npos,
           "sharpness exclusive_set contains minecraft:smite");

    expect(smite_content.find("anvil_cost") != std::string::npos,
           "smite content contains anvil_cost");
    expect(smite_content.find("exclusive_set") != std::string::npos,
           "smite content contains exclusive_set");
    expect(smite_content.find("minecraft:sharpness") != std::string::npos,
           "smite exclusive_set contains minecraft:sharpness");

    std::cout << "PASS: test_to_mc_official_strings" << std::endl;
}

// ─── Main ──────────────────────────────────────────────────────────────

int main() {
    try {
        test_serialize_ench();
        test_serialize_enchinfo();
        test_serialize_enchset();
        test_serialize_equipment();
        test_serialize_equipment_tag();
        test_serialize_item();
        test_serialize_solution_step();
        test_serialize_solution();
        test_serialize_equipment_registry();
        test_serialize_equipment_tag_registry();
        test_serialize_ench_registry();
        test_serializer_mce_helpers();
        test_serializer_to_from_string();
        test_to_mc_official_strings();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
        return 1;
    }
    return print_summary();
}
