#define BESQ_TEST_MAIN
#include "domain/business/registries/EnchantmentRegistry.h"
#include "domain/business/registries/EquipmentRegistry.h"
#include "domain/interface/cli/InventoryParser.h"
#include "domain/interface/cli/InventorySchema.h"
#include "ds/ds.h"
#include "framework/test_framework.h"
#include <filesystem>
#include <fstream>
#include <string>

// ============================================================================
// Helper: minimal registries
// ============================================================================
static EquipmentRegistry make_eq_reg() {
    EquipmentRegistry reg;
    reg.insert(Equipment{NSID("minecraft:diamond_sword"), "Diamond Sword", NSID("#minecraft:sword"), 1561});
    reg.insert(Equipment{NSID("minecraft:netherite_helmet"), "Netherite Helmet", NSID("#minecraft:helmet"), 407});
    return reg;
}

static EnchantmentRegistry make_ench_reg() {
    EnchantmentRegistry reg;
    reg.insert(EnchInfo{NSID("minecraft:sharpness"), "Sharpness", MCE::All, 5, 5, 1, false, {}, {NSID("#minecraft:sword")}});
    reg.insert(EnchInfo{NSID("minecraft:knockback"), "Knockback", MCE::All, 2, 2, 2, false, {}, {NSID("#minecraft:sword")}});
    reg.insert(EnchInfo{NSID("minecraft:protection"), "Protection", MCE::All, 4, 4, 1, false, {}, {NSID("#minecraft:helmet")}});
    return reg;
}

// Unique temp file path for an inventory JSON payload.
static std::string write_temp(const std::string& content) {
    static int counter = 0;
    auto path = std::filesystem::temp_directory_path() / ("besq_inv_test_" + std::to_string(++counter) + ".json");
    std::ofstream f(path);
    f << content;
    return path.string();
}

// A valid diamond-sword target fragment (JSON object member, no trailing comma).
static const char* TARGET = R"("target": {
    "item": "diamond_sword",
    "enchants": [ {"id": "sharpness", "level": 5}, {"id": "knockback", "level": 2} ]
})";

/// Build a full task JSON string: { <TARGET>, "items": [items] [, extra] }.
static std::string task_json(const std::string& items, const std::string& extra = "") {
    std::string out = "{\n" + std::string(TARGET) + ",\n\"items\": [" + items + "]";
    if (!extra.empty())
        out += ",\n" + extra;
    out += "\n}";
    return out;
}

// ============================================================================
// Tests
// ============================================================================

TEST_CASE("test_parse_valid") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto path = write_temp(task_json(
        R"({ "type": "book", "enchants": [{"id":"sharpness","level":5}],
             "prior_penalty": 0, "priority": 1 },
           { "type": "equipment", "id": "diamond_sword", "enchants": [],
             "prior_penalty": 2, "durability": 500, "priority": 10 })",
        R"("algorithm": "dp_merge", "profile": "modded_sword")"));
    auto inv = InventoryParser::parse_file(path, ench_reg, eq_reg);

    expect(inv.target_item.id == NSID("minecraft:diamond_sword"), "target item is the diamond sword");
    expect(inv.target_item.enchantments.size() == 2, "target carries 2 enchants");
    expect(inv.items.size() == 2, "two items parsed");
    expect(inv.items[0].is_book(), "first item is a book");
    expect(inv.items[0].enchantments.size() == 1, "book has 1 enchant");
    expect(inv.items[0].prior_penalty == 0, "book prior_penalty");
    expect(inv.items[1].id == NSID("minecraft:diamond_sword"), "second item is the diamond sword");
    expect(inv.items[1].prior_penalty == 2, "equipment prior_penalty");
    expect(inv.items[1].durability == 500, "explicit durability respected");
    expect(inv.priorities.size() == 2, "two priorities");
    expect(inv.priorities[0] == 1, "book priority 1");
    expect(inv.priorities[1] == 10, "equipment priority 10");
    expect(inv.algorithm == "dp_merge", "algorithm parsed from file");
    expect(inv.profile == "modded_sword", "profile parsed from file");

    TEST_PASS("test_parse_valid");
}

TEST_CASE("test_parse_empty_items") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto path = write_temp(task_json(""));
    auto inv = InventoryParser::parse_file(path, ench_reg, eq_reg);
    expect(inv.items.empty(), "empty items list yields empty result");
    expect(inv.target_item.id == NSID("minecraft:diamond_sword"), "target still parsed when items is empty");
    TEST_PASS("test_parse_empty_items");
}

TEST_CASE("test_unknown_equipment_throws") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto path = write_temp(task_json(R"({ "type": "equipment", "id": "not_a_real_sword", "enchants": [] })"));
    bool threw = false;
    try {
        InventoryParser::parse_file(path, ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "unknown equipment id should throw");
    TEST_PASS("test_unknown_equipment_throws");
}

TEST_CASE("test_unknown_ench_throws") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto path = write_temp(task_json(R"({ "type": "book", "enchants": [{"id":"nonexistent","level":1}] })"));
    bool threw = false;
    try {
        InventoryParser::parse_file(path, ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "unknown enchantment should throw");
    TEST_PASS("test_unknown_ench_throws");
}

TEST_CASE("test_book_over_level_ench_throws") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto path = write_temp(task_json(R"({ "type": "book", "enchants": [{"id":"sharpness","level":10}] })"));
    // sharpness max_level is 5
    bool threw = false;
    try {
        InventoryParser::parse_file(path, ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "book enchantment level exceeding max should throw");
    TEST_PASS("test_book_over_level_ench_throws");
}

TEST_CASE("test_equipment_over_level_ench_throws") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto path = write_temp(task_json(
        R"({ "type": "equipment", "id": "diamond_sword",
             "enchants": [{"id":"knockback","level":9}] })"));
    // knockback max_level is 2
    bool threw = false;
    try {
        InventoryParser::parse_file(path, ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "equipment enchantment level exceeding max should throw");
    TEST_PASS("test_equipment_over_level_ench_throws");
}

TEST_CASE("test_bad_type_throws") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto path = write_temp(task_json(R"({ "type": "sword", "enchants": [] })"));
    bool threw = false;
    try {
        InventoryParser::parse_file(path, ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "invalid item type should throw");
    TEST_PASS("test_bad_type_throws");
}

TEST_CASE("test_missing_file_throws") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        InventoryParser::parse_file("no_such_inventory_file.json", ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "missing file should throw");
    TEST_PASS("test_missing_file_throws");
}

// ── target parsing ─────────────────────────────────────────────────────

TEST_CASE("test_target_equipment") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto inv = InventoryParser::parse_string(R"({
        "target": { "item": "diamond_sword", "enchants": [{"id":"sharpness","level":5}] },
        "items": []
    })",
                                             ench_reg, eq_reg);
    expect(inv.target_item.id == NSID("minecraft:diamond_sword"), "target resolves to diamond_sword");
    expect(inv.target_item.enchantments.size() == 1, "target has sharpness");
    expect(inv.target_item.prior_penalty == 0, "target starts with prior_penalty 0");
    expect(inv.target_item.durability == 1561, "target starts at full durability");
    TEST_PASS("test_target_equipment");
}

TEST_CASE("test_target_book") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto inv = InventoryParser::parse_string(R"({
        "target": { "item": "book", "enchants": [{"id":"sharpness","level":5}] },
        "items": []
    })",
                                             ench_reg, eq_reg);
    expect(inv.target_item.is_book(), "book target is a book");
    expect(inv.target_item.id == NSID("minecraft:enchanted_book"), "book normalises to enchanted_book");
    expect(inv.target_item.durability == 0, "book has no durability");
    TEST_PASS("test_target_book");
}

TEST_CASE("test_target_unknown_equipment_throws") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        InventoryParser::parse_string(R"({
            "target": { "item": "not_a_real_sword", "enchants": [] },
            "items": []
        })",
                                      ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "unknown target equipment should throw");
    TEST_PASS("test_target_unknown_equipment_throws");
}

TEST_CASE("test_target_over_level_ench_throws") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        InventoryParser::parse_string(R"({
            "target": { "item": "diamond_sword", "enchants": [{"id":"sharpness","level":10}] },
            "items": []
        })",
                                      ench_reg, eq_reg); // sharpness max_level is 5
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "target enchantment level exceeding max should throw");
    TEST_PASS("test_target_over_level_ench_throws");
}

TEST_CASE("test_target_missing_entirely") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto inv = InventoryParser::parse_string(R"({
        "target": { "item": "", "enchants": [] },
        "items": []
    })",
                                             ench_reg, eq_reg);
    expect(inv.target_item.id.empty(), "empty target item leaves target_item empty");
    TEST_PASS("test_target_missing_entirely");
}

// ── algorithm / profile parsing ────────────────────────────────────────

TEST_CASE("test_algorithm_present") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto inv = InventoryParser::parse_string(R"({
        "target": { "item": "diamond_sword", "enchants": [] },
        "items": [],
        "algorithm": "dp_merge"
    })",
                                             ench_reg, eq_reg);
    expect(inv.algorithm == "dp_merge", "algorithm set from file");
    TEST_PASS("test_algorithm_present");
}

TEST_CASE("test_algorithm_absent") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto inv = InventoryParser::parse_string(R"({
        "target": { "item": "diamond_sword", "enchants": [] },
        "items": []
    })",
                                             ench_reg, eq_reg);
    expect(inv.algorithm.empty(), "algorithm empty when not specified");
    TEST_PASS("test_algorithm_absent");
}

TEST_CASE("test_profile_present") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto inv = InventoryParser::parse_string(R"({
        "target": { "item": "diamond_sword", "enchants": [] },
        "items": [],
        "profile": "modded_sword"
    })",
                                             ench_reg, eq_reg);
    expect(inv.profile == "modded_sword", "profile set from file");
    TEST_PASS("test_profile_present");
}

// ── stdin path (parse_string is the shared core parse_file("-") delegates to) ──

TEST_CASE("test_parse_string_roundtrip") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto inv = InventoryParser::parse_string(R"({
        "target": { "item": "diamond_sword", "enchants": [] },
        "items": [ { "type": "book", "enchants": [{"id":"sharpness","level":5}],
                     "priority": 3 } ]
    })",
                                             ench_reg, eq_reg);
    expect(inv.items.size() == 1, "parse_string parses items");
    expect(inv.items[0].is_book(), "parse_string item is a book");
    expect(inv.priorities.size() == 1 && inv.priorities[0] == 3, "parse_string carries priorities");
    TEST_PASS("test_parse_string_roundtrip");
}

// ── ds schema errors ───────────────────────────────────────────────────

// Direct ds-level verification (independent of i18n): `required_field` on a
// nested `object_codec` must report a missing `target`.
TEST_CASE("test_ds_reports_missing_target") {
    Json root = Json::parse(R"({ "items": [] })");
    InvTaskDto dto;
    ds::ErrorList err;
    bool ok = InvTaskJson::parse(root, dto, err);
    expect(!ok, "missing target key fails the schema parse");
    expect(err.str().find("target") != std::string::npos, "schema error names the target field");
    TEST_PASS("test_ds_reports_missing_target");
}

TEST_CASE("test_missing_target_key_throws") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    std::string msg;
    try {
        InventoryParser::parse_string(R"({ "items": [] })", ench_reg, eq_reg);
    } catch (const std::runtime_error& e) {
        threw = true;
        msg = e.what();
    }
    expect(threw, "missing target key should throw a schema error");
    // i18n may not be loaded in the standalone test binary, so the message is
    // either the raw key or the resolved English text.
    expect(msg.find("inventory_schema_error") != std::string::npos || msg.find("schema error") != std::string::npos,
           "error is the inventory schema error");
    TEST_PASS("test_missing_target_key_throws");
}

TEST_CASE("test_wrong_target_type_throws") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    std::string msg;
    try {
        InventoryParser::parse_string(R"({ "target": "diamond_sword", "items": [] })", ench_reg, eq_reg);
    } catch (const std::runtime_error& e) {
        threw = true;
        msg = e.what();
    }
    expect(threw, "string-typed target should throw a schema error");
    expect(msg.find("inventory_schema_error") != std::string::npos || msg.find("schema error") != std::string::npos,
           "error is the inventory schema error");
    TEST_PASS("test_wrong_target_type_throws");
}

TEST_CASE("test_unknown_root_keys_tolerated") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto inv = InventoryParser::parse_string(R"({
        "name": "My Inventory", "description": "demo", "author": "me", "version": "1.0",
        "target": { "item": "diamond_sword", "enchants": [] },
        "items": []
    })",
                                             ench_reg, eq_reg);
    expect(inv.items.empty(), "decorative root keys are tolerated");
    expect(inv.target_item.id == NSID("minecraft:diamond_sword"), "target parsed despite decorative root keys");
    TEST_PASS("test_unknown_root_keys_tolerated");
}

// ── item bounds / duplicates ───────────────────────────────────────────

TEST_CASE("test_durability_exceeds_max_throws") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        InventoryParser::parse_string(R"({
            "target": { "item": "diamond_sword", "enchants": [] },
            "items": [ { "type": "equipment", "id": "diamond_sword", "durability": 2000 } ]
        })",
                                      ench_reg, eq_reg); // diamond_sword max_durability is 1561
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "durability above max_durability should throw");
    TEST_PASS("test_durability_exceeds_max_throws");
}

TEST_CASE("test_prior_penalty_negative_throws") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        InventoryParser::parse_string(R"({
            "target": { "item": "diamond_sword", "enchants": [] },
            "items": [ { "type": "book", "enchants": [], "prior_penalty": -1 } ]
        })",
                                      ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "negative prior_penalty should throw");
    TEST_PASS("test_prior_penalty_negative_throws");
}

TEST_CASE("test_prior_penalty_over_255_throws") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        InventoryParser::parse_string(R"({
            "target": { "item": "diamond_sword", "enchants": [] },
            "items": [ { "type": "book", "enchants": [], "prior_penalty": 256 } ]
        })",
                                      ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "prior_penalty above 255 should throw");
    TEST_PASS("test_prior_penalty_over_255_throws");
}

TEST_CASE("test_prior_penalty_255_ok") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto inv = InventoryParser::parse_string(R"({
        "target": { "item": "diamond_sword", "enchants": [] },
        "items": [ { "type": "book", "enchants": [], "prior_penalty": 255 } ]
    })",
                                             ench_reg, eq_reg);
    expect(inv.items.size() == 1 && inv.items[0].prior_penalty == 255, "prior_penalty at max 255 is allowed");
    TEST_PASS("test_prior_penalty_255_ok");
}

TEST_CASE("test_duplicate_ench_throws") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        InventoryParser::parse_string(R"({
            "target": { "item": "diamond_sword", "enchants": [] },
            "items": [ { "type": "book",
                         "enchants": [{"id":"sharpness","level":5},{"id":"sharpness","level":3}] } ]
        })",
                                      ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "duplicate enchantment in an item should throw");
    TEST_PASS("test_duplicate_ench_throws");
}

TEST_CASE("test_duplicate_target_ench_throws") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    bool threw = false;
    try {
        InventoryParser::parse_string(R"({
            "target": { "item": "diamond_sword",
                        "enchants": [{"id":"sharpness","level":5},{"id":"sharpness","level":3}] },
            "items": []
        })",
                                      ench_reg, eq_reg);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    expect(threw, "duplicate enchantment in target should throw");
    TEST_PASS("test_duplicate_target_ench_throws");
}

// ── schema shape ───────────────────────────────────────────────────────

TEST_CASE("test_items_key_absent") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto inv = InventoryParser::parse_string(R"({
        "target": { "item": "diamond_sword", "enchants": [] }
    })",
                                             ench_reg, eq_reg);
    expect(inv.items.empty(), "absent items key yields an empty items list");
    expect(inv.target_item.id == NSID("minecraft:diamond_sword"), "target parsed without an items key");
    TEST_PASS("test_items_key_absent");
}

// Empty target item string drops any listed enchants (documented behaviour —
// the parser skips target construction entirely).
TEST_CASE("test_target_empty_item_with_enchants") {
    auto eq_reg = make_eq_reg();
    auto ench_reg = make_ench_reg();
    auto inv = InventoryParser::parse_string(R"({
        "target": { "item": "", "enchants": [{"id":"sharpness","level":5}] },
        "items": []
    })",
                                             ench_reg, eq_reg);
    expect(inv.target_item.id.empty(), "empty target item leaves target_item empty");
    TEST_PASS("test_target_empty_item_with_enchants");
}

// ============================================================================
// main
// ============================================================================